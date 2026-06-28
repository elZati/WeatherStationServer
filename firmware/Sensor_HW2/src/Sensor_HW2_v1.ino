/*
 * =========================================================================
 * SENSOR HW 2.0 FIRMWARE (v1.1)
 * -------------------------------------------------------------------------
 * Target:   ESP32-C3 SuperMini (HW-466AB)
 * Radio:    NRF24L01+PA — CE=GPIO0, CSN=GPIO1
 *
 * SW1 DIP switch (active-low, pull-up; other side of all switches → GND):
 *   P1 (GPIO20) = NODE_ID bit 2 (value 4) ─┐
 *   P2 (GPIO21) = NODE_ID bit 1 (value 2)  ├─ NODE_ID 1–5
 *   P3 (GPIO3)  = NODE_ID bit 0 (value 1) ─┘
 *
 * Operating mode is auto-detected at boot by I2C scan:
 *
 *   BATTERY mode — BME280 only (no ENS160 found)
 *     · ESP32 deep sleep between transmissions (full reboot on wake)
 *     · sleep_multiplier persists in RTC SRAM across deep-sleep cycles
 *     · sensor4 = 0.0 (no battery ADC circuit in HW 2.0 schematic)
 *
 *   USB mode — BME280 + ENS160 found
 *     · delay() between transmissions — keeps ENS160 baseline alive
 *     · Deep sleep would reset ENS160 gas-sensor state every cycle
 *     · temp/humidity fed to ENS160 each cycle for compensation
 *     · sensor4 = 0.0 (USB powered)
 *
 * Protocol: 25-byte SensorPayloadV2 (server identifies by payload size:
 *           25 b = HW 2.0/3.0, 20 b = HW 1.x legacy).
 *
 * Libraries (PlatformIO):
 *   adafruit/Adafruit BME280 Library
 *   adafruit/Adafruit Unified Sensor
 *   nrf24/RF24
 *   sciosense/ScioSense_ENS160
 *
 * PlatformIO board: esp32-c3-devkitm-1  (or lolin_c3_mini)
 * =========================================================================
 */

#include <SPI.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <ScioSense_ENS160.h>
#include <RF24.h>

// =========================================================================
// HARDWARE PINS
// =========================================================================
#define I2C_SDA   4
#define I2C_SCL   5
#define NRF_CE    0
#define NRF_CSN   1
#define NRF_SCK  10
#define NRF_MISO  6
#define NRF_MOSI  7

// DIP switch SW1 (active-low, internal pull-up): P1=GPIO20 (bit2), P2=GPIO21 (bit1), P3=GPIO3 (bit0)
// NODE_ID = sum of closed switch values: P1=4, P2=2, P3=1. Valid range: 1–5.
#define SW_P1    20   // bit 2, value 4
#define SW_P2    21   // bit 1, value 2
#define SW_P3     3   // bit 0, value 1

// =========================================================================
// 25-BYTE PAYLOAD — keep in sync with SensorPayloadV2 in Server_v15.cpp
// =========================================================================
#pragma pack(push, 1)
struct SensorPayloadV2 {
    int32_t  nodeID;   // 4 bytes
    float    sensor1;  // 4 bytes — temperature °C
    float    sensor2;  // 4 bytes — humidity %RH
    float    sensor3;  // 4 bytes — pressure hPa
    float    sensor4;  // 4 bytes — 0.0 (no battery ADC in HW 2.0 schematic)
    uint16_t eco2;     // 2 bytes — eCO2 ppm  (400–65000; 0 if no ENS160)
    uint16_t tvoc;     // 2 bytes — TVOC ppb  (0–65000;   0 if no ENS160)
    uint8_t  aqi;      // 1 byte  — AQI 1–5   (0 = warming up or no ENS160)
};                     // total: 25 bytes
#pragma pack(pop)

// =========================================================================
// ADDRESSING
// =========================================================================
const uint64_t BASE_ADDR = 0xABCDABCD00LL;

// =========================================================================
// GLOBALS
// RTC_DATA_ATTR survives deep sleep; plain globals reset on each deep-sleep wake.
// =========================================================================
RTC_DATA_ATTR uint8_t sleep_multiplier = 5;   // 5 × 8 s = 40 s default

int32_t  NODE_ID = 0;   // re-read from DIP switch every boot
uint64_t MY_ADDR = 0;
bool     hasBME  = false;
bool     hasENS  = false;
bool     usbMode = false;

SPIClass         spiNrf(FSPI);
RF24             radio(NRF_CE, NRF_CSN);
Adafruit_BME280  bme;
ScioSense_ENS160 ens160;

// =========================================================================
// I2C SCANNER
// =========================================================================
void scanI2C() {
    Serial.println("I2C scan:");
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  0x%02X", addr);
            if      (addr == 0x76 || addr == 0x77) Serial.print("  BME280");
            else if (addr == 0x52 || addr == 0x53) Serial.print("  ENS160");
            Serial.println();
            found++;
        }
    }
    if (!found) Serial.println("  none");
}

// =========================================================================
// TRANSMIT CYCLE — shared by both modes
// =========================================================================
void runTransmitCycle() {
    SensorPayloadV2 pkt = {};
    pkt.nodeID  = NODE_ID;
    pkt.sensor4 = 0.0f;   // no battery ADC circuit on HW 2.0

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

    if (usbMode) {
        Serial.printf("T=%.1f H=%.1f P=%.1f eCO2=%u TVOC=%u AQI=%u\n",
                      pkt.sensor1, pkt.sensor2, pkt.sensor3,
                      pkt.eco2, pkt.tvoc, pkt.aqi);
    } else {
        Serial.printf("T=%.1f H=%.1f P=%.1f\n",
                      pkt.sensor1, pkt.sensor2, pkt.sensor3);
    }

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
}

// =========================================================================
// SETUP — runs on every power-on AND every deep-sleep wake (battery mode)
// =========================================================================
void setup() {
    // ---- DIP switch: read NODE_ID ----
    pinMode(SW_P1, INPUT_PULLUP);
    pinMode(SW_P2, INPUT_PULLUP);
    pinMode(SW_P3, INPUT_PULLUP);
    delay(10);
    NODE_ID = ((!digitalRead(SW_P1)) << 2) |
              ((!digitalRead(SW_P2)) << 1) |
              ((!digitalRead(SW_P3)) << 0);
    MY_ADDR = BASE_ADDR + NODE_ID;

    Serial.begin(115200);
    delay(100);
    Serial.printf("\n=== NODE %d HW2.0 START ===\n", NODE_ID);

    if (NODE_ID < 1 || NODE_ID > 5) {
        Serial.printf("CRITICAL: Invalid NODE_ID=%d — set SW1 to 1-5 and reboot\n", NODE_ID);
        while (1) delay(1000);
    }

    // ---- I2C scan + sensor init ----
    Wire.begin(I2C_SDA, I2C_SCL);
    scanI2C();

    hasBME = bme.begin(0x76, &Wire);
    if (hasBME) {
        Serial.println("BME280: OK");
    } else {
        Serial.println("WARN: BME280 not found");
    }

    hasENS = ens160.begin();
    if (hasENS) {
        ens160.setMode(ENS160_OPMODE_STD);
        Serial.println("ENS160: OK (STD mode, ~1h warm-up)");
    } else {
        Serial.println("ENS160: not present");
    }

    // ---- Operating mode ----
    usbMode = hasENS;
    if (usbMode) {
        Serial.println("Mode: USB — delay() sleep, ENS160 continuous");
    } else {
        Serial.println("Mode: BATTERY — ESP32 deep sleep between cycles");
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

    Serial.println("Setup complete");

    // ---- Battery mode: transmit once then deep sleep (never enters loop) ----
    if (!usbMode) {
        runTransmitCycle();

        uint32_t sleep_s = (uint32_t)sleep_multiplier * 8;
        Serial.printf("Deep sleep %u s\n", sleep_s);
        Serial.flush();
        delay(20);   // let serial drain before powering down

        esp_sleep_enable_timer_wakeup((uint64_t)sleep_s * 1000000ULL);
        esp_deep_sleep_start();
        // execution never continues past here — next wake restarts setup()
    }
}

// =========================================================================
// LOOP — USB mode only (battery mode never reaches here)
// =========================================================================
void loop() {
    runTransmitCycle();

    uint32_t sleep_ms = (uint32_t)sleep_multiplier * 8000UL;
    Serial.printf("Waiting %u s\n\n", sleep_ms / 1000);
    delay(sleep_ms);
}
