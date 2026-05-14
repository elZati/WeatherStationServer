/*
 * =========================================================================
 * SENSOR HW 2.0 FIRMWARE (v1.0)
 * -------------------------------------------------------------------------
 * Target:   ESP32-C3 SuperMini (HW-466AB)
 * Sensors:  GY-BME280 (0x76) — Temperature / Humidity / Pressure
 *           ENS160    (0x52) — eCO2 / TVOC / AQI
 * Radio:    NRF24L01+PA — CE=GPIO0, CSN=GPIO1
 *
 * Protocol: 25-byte SensorPayloadV2.  Server identifies HW 2.0 nodes by
 *           dynamic payload size (25 bytes vs legacy 20 bytes).
 *
 * Sleep:    delay() between transmissions (not deep-sleep).  This node is
 *           bench-powered; the ENS160 requires continuous operation to build
 *           its gas baseline — deep-sleep would reset it every cycle.
 *           Sleep duration = sleep_multiplier × 8 s (updated via ACK payload).
 *
 * Libraries (PlatformIO / Arduino Library Manager):
 *   adafruit/Adafruit BME280 Library
 *   adafruit/Adafruit Unified Sensor
 *   nrf24/RF24
 *   sciosense/ScioSense_ENS160  (or dfrobot/DFRobot_ENS160)
 *
 * PlatformIO board: esp32-c3-devkitm-1  (or lolin_c3_mini)
 * =========================================================================
 */

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <ScioSense_ENS160.h>
#include <RF24.h>

// =========================================================================
// NODE CONFIGURATION — set a unique ID (1–5) for each physical node
// =========================================================================
const int32_t NODE_ID = 4;

// =========================================================================
// HARDWARE PINS — matches sensor_node_project.md pinout for HW-466AB
// =========================================================================
#define I2C_SDA   4
#define I2C_SCL   5
#define NRF_CE    0
#define NRF_CSN   1
#define NRF_SCK  10
#define NRF_MISO  6
#define NRF_MOSI  7

// =========================================================================
// 25-BYTE PAYLOAD (packed — server detects HW 2.0 by this exact size)
// IMPORTANT: keep in sync with SensorPayloadV2 in Server_v15.cpp
// =========================================================================
#pragma pack(push, 1)
struct SensorPayloadV2 {
    int32_t  nodeID;   // 4 bytes
    float    sensor1;  // 4 bytes — temperature °C
    float    sensor2;  // 4 bytes — humidity %RH
    float    sensor3;  // 4 bytes — pressure hPa
    float    sensor4;  // 4 bytes — 0.0 (USB/bench power; no battery circuit)
    uint16_t eco2;     // 2 bytes — eCO2 ppm  (400–65000)
    uint16_t tvoc;     // 2 bytes — TVOC ppb  (0–65000)
    uint8_t  aqi;      // 1 byte  — AQI 1–5 (0 = warming up)
};                     // total: 25 bytes
#pragma pack(pop)

// =========================================================================
// ADDRESSING — baseAddress + NODE_ID must match server pipes[]
// =========================================================================
const uint64_t BASE_ADDR = 0xABCDABCD00LL;
const uint64_t MY_ADDR   = BASE_ADDR + NODE_ID;

// =========================================================================
// GLOBALS
// =========================================================================
SPIClass         spiNrf(FSPI);
RF24             radio(NRF_CE, NRF_CSN);
Adafruit_BME280  bme;
ScioSense_ENS160 ens160;

bool     hasBME          = false;
bool     hasENS          = false;
uint8_t  sleep_multiplier = 5;   // 5 × 8 s = 40 s default

// =========================================================================
// SETUP — runs once on power-on
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.printf("\n=== NODE %d HW2.0 START ===\n", NODE_ID);

    // ---- I2C ----
    Wire.begin(I2C_SDA, I2C_SCL);

    hasBME = bme.begin(0x76, &Wire);
    if (!hasBME) Serial.println("WARN: BME280 not found at 0x76");

    hasENS = ens160.begin();
    if (hasENS) {
        ens160.setMode(ENS160_OPMODE_STD);
        Serial.println("ENS160: STD mode — warming up (~1h for stable baseline)");
    } else {
        Serial.println("WARN: ENS160 not found at 0x52");
    }

    // ---- SPI + NRF24 ----
    spiNrf.begin(NRF_SCK, NRF_MISO, NRF_MOSI, NRF_CSN);

    if (!radio.begin(&spiNrf)) {
        Serial.println("CRITICAL: Radio not found — halting");
        while (1) delay(1000);
    }

    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.setAutoAck(true);
    radio.enableDynamicPayloads();
    radio.enableAckPayload();
    radio.setPALevel(RF24_PA_LOW);
    radio.setRetries(5, 15);
    radio.openWritingPipe(MY_ADDR);

    Serial.println("Setup complete — starting transmit loop");
}

// =========================================================================
// LOOP — transmit cycle; delay() keeps ENS160 running continuously
// =========================================================================
void loop() {
    SensorPayloadV2 pkt = {};
    pkt.nodeID  = NODE_ID;
    pkt.sensor4 = 0.0f;

    if (hasBME) {
        pkt.sensor1 = bme.readTemperature();
        pkt.sensor2 = bme.readHumidity();
        pkt.sensor3 = bme.readPressure() / 100.0f;
    }

    if (hasENS) {
        if (hasBME) ens160.set_envdata(pkt.sensor1, pkt.sensor2);
        if (ens160.measure(true)) {
            pkt.aqi  = ens160.getAQI();
            pkt.eco2 = ens160.geteCO2();
            pkt.tvoc = ens160.getTVOC();
        }
    }

    Serial.printf("T=%.1f H=%.1f P=%.1f eCO2=%u TVOC=%u AQI=%u\n",
                   pkt.sensor1, pkt.sensor2, pkt.sensor3,
                   pkt.eco2, pkt.tvoc, pkt.aqi);

    radio.powerUp();
    delay(10);
    bool ok = radio.write(&pkt, sizeof(pkt));

    if (ok) {
        Serial.println("TX: OK");
        if (radio.isAckPayloadAvailable()) {
            float cmd;
            radio.read(&cmd, sizeof(float));
            if (cmd >= 1 && cmd <= 200) {
                sleep_multiplier = (uint8_t)cmd;
                Serial.printf("ACK: sleep_mult=%u\n", sleep_multiplier);
            }
        }
    } else {
        Serial.println("TX: FAILED");
    }

    radio.powerDown();

    uint32_t sleep_ms = (uint32_t)sleep_multiplier * 8000;
    Serial.printf("Waiting %u s\n\n", sleep_ms / 1000);
    delay(sleep_ms);
}
