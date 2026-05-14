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
 * Sleep:    ESP32 deep-sleep between transmissions.  setup() re-runs on
 *           every wake — there is no persistent loop().  Sleep duration
 *           (sleep_multiplier × 8 s) is stored in RTC_DATA_ATTR so it
 *           survives deep-sleep but resets to the default on power-off.
 *
 * Libraries (PlatformIO / Arduino Library Manager):
 *   adafruit/Adafruit BME280 Library
 *   adafruit/Adafruit Unified Sensor
 *   tmrh20/RF24
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
#include <esp_sleep.h>

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
// RTC MEMORY — persists through deep-sleep, reset on power-off
// =========================================================================
RTC_DATA_ATTR uint8_t sleep_multiplier = 5;  // default: 5 × 8 s = 40 s

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
// HARDWARE OBJECTS
// =========================================================================
SPIClass         spiNrf(FSPI);   // dedicated SPI instance for NRF24
RF24             radio(NRF_CE, NRF_CSN);
Adafruit_BME280  bme;
ScioSense_ENS160 ens160;

// =========================================================================
// SETUP — runs on every wake cycle (deep-sleep resets to here)
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.printf("\n=== NODE %d HW2.0 WAKE | sleep_mult=%u ===\n",
                  NODE_ID, sleep_multiplier);

    // ---- I2C (BME280 + ENS160) ----
    Wire.begin(I2C_SDA, I2C_SCL);

    bool hasBME = bme.begin(0x76, &Wire);
    if (!hasBME) Serial.println("WARN: BME280 not found at 0x76");

    bool hasENS = ens160.begin();
    if (hasENS) {
        ens160.setMode(ENS160_OPMODE_STD);
    } else {
        Serial.println("WARN: ENS160 not found at 0x52");
    }

    // ---- SPI + NRF24 ----
    spiNrf.begin(NRF_SCK, NRF_MISO, NRF_MOSI, NRF_CSN);

    if (!radio.begin(&spiNrf)) {
        Serial.println("CRITICAL: Radio not found — sleeping");
        goto do_sleep;
    }

    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.setAutoAck(true);
    radio.enableDynamicPayloads();
    radio.enableAckPayload();
    radio.setPALevel(RF24_PA_LOW);  // raise to RF24_PA_HIGH if range is poor
    radio.setRetries(5, 15);
    radio.openWritingPipe(MY_ADDR);

    // ---- Build payload ----
    {
        SensorPayloadV2 pkt = {};
        pkt.nodeID  = NODE_ID;
        pkt.sensor4 = 0.0f;    // no battery circuit on HW 2.0

        if (hasBME) {
            pkt.sensor1 = bme.readTemperature();
            pkt.sensor2 = bme.readHumidity();
            pkt.sensor3 = bme.readPressure() / 100.0f;
        }

        if (hasENS) {
            // ENS160 accuracy improves when fed live temperature+humidity
            if (hasBME) ens160.set_envdata(pkt.sensor1, pkt.sensor2);

            if (ens160.measure(true)) {
                pkt.aqi  = ens160.getAQI();
                pkt.eco2 = ens160.geteCO2();
                pkt.tvoc = ens160.getTVOC();
            }
            // aqi=0 when ENS160 is still in startup/warm-up (normal in first hour)
        }

        Serial.printf("T=%.1f H=%.1f P=%.1f eCO2=%u TVOC=%u AQI=%u\n",
                       pkt.sensor1, pkt.sensor2, pkt.sensor3,
                       pkt.eco2, pkt.tvoc, pkt.aqi);
        Serial.printf("Payload size: %u bytes\n", (unsigned)sizeof(pkt));

        // ---- Transmit ----
        radio.powerUp();
        delay(10);
        bool ok = radio.write(&pkt, sizeof(pkt));

        if (ok) {
            Serial.println("TX: SUCCESS (ACK received)");
            if (radio.isAckPayloadAvailable()) {
                float cmd;
                radio.read(&cmd, sizeof(float));
                if (cmd >= 1 && cmd <= 200) {
                    sleep_multiplier = (uint8_t)cmd;
                    Serial.printf("ACK: new sleep_mult = %u\n", sleep_multiplier);
                }
            }
        } else {
            Serial.println("TX: FAILED (no response)");
        }
        radio.powerDown();
    }

do_sleep:
    uint32_t sleep_sec = (uint32_t)sleep_multiplier * 8;
    Serial.printf("Sleeping %u s\n\n", sleep_sec);
    Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)sleep_sec * 1000000ULL);
    esp_deep_sleep_start();
    // execution never reaches here
}

void loop() {}  // never called — deep-sleep resets to setup()
