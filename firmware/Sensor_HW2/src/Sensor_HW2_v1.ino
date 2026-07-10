/*
 * =========================================================================
 * SENSOR HW 2.0 FIRMWARE (v1.2)
 * -------------------------------------------------------------------------
 * Target:   ESP32-C3 SuperMini (HW-466AB)
 * Radio:    NRF24L01 — CE=GPIO0, CSN=GPIO1
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
 *     · WiFi/OTA/Telnet disabled — battery conservation
 *
 *   USB mode — BME280 + ENS160 found
 *     · Non-blocking loop — ArduinoOTA + Telnet stay responsive
 *     · ENS160 baseline maintained continuously (no sleep)
 *     · OTA hostname: hw20-node4.local   password: kuningas
 *     · Telnet monitor: telnet <ip> 23
 *
 * Protocol: 25-byte SensorPayloadV2 (server identifies by payload size:
 *           25 b = HW 2.0/3.0, 20 b = HW 1.x legacy).
 * =========================================================================
 */

#include <SPI.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <ScioSense_ENS160.h>
#include <RF24.h>

// =========================================================================
// WIFI / OTA / TELNET
// =========================================================================
#define WIFI_SSID  "VV_NETTI"
#define WIFI_PASS  "@kuningas84"
#define OTA_PASS   "kuningas"
#define OTA_HOST   "hw20-node4"

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

// DIP switch SW1 (active-low, internal pull-up): P1=GPIO20, P2=GPIO21, P3=GPIO3
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
    float    sensor4;  // 4 bytes — 0.0 (no battery ADC in HW 2.0)
    uint16_t eco2;     // 2 bytes — eCO2 ppm  (0 if no ENS160)
    uint16_t tvoc;     // 2 bytes — TVOC ppb  (0 if no ENS160)
    uint8_t  aqi;      // 1 byte  — AQI 1–5   (0 = warming up or no ENS160)
};                     // total: 25 bytes
#pragma pack(pop)

// =========================================================================
// ADDRESSING
// =========================================================================
const uint64_t BASE_ADDR = 0xABCDABCD00LL;

// =========================================================================
// GLOBALS
// =========================================================================
RTC_DATA_ATTR uint8_t sleep_multiplier = 5;   // 5 × 8 s = 40 s default

int32_t  NODE_ID = 0;
uint64_t MY_ADDR = 0;
bool     hasBME  = false;
bool     hasENS  = false;
bool     usbMode = false;

SPIClass         spiNrf(FSPI);
RF24             radio(NRF_CE, NRF_CSN);
Adafruit_BME280  bme;
ScioSense_ENS160 ens160;

WiFiServer  telnetServer(23);
WiFiClient  telnetClient;

// =========================================================================
// TPRINTF — print to Serial and active Telnet client simultaneously
// =========================================================================
void tprintf(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.print(buf);
    if (telnetClient && telnetClient.connected())
        telnetClient.print(buf);
}

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
// WIFI SETUP
// =========================================================================
void setupWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("WiFi connecting");
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nWiFi OK  IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi FAILED — continuing without network");
    }
}

// =========================================================================
// OTA SETUP
// =========================================================================
void setupOTA() {
    ArduinoOTA.setHostname(OTA_HOST);
    ArduinoOTA.setPassword(OTA_PASS);
    ArduinoOTA.onStart([]() {
        radio.powerDown();
        Serial.println("OTA: update starting");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA: done — rebooting");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA: %u%%\r", progress * 100 / total);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA error [%u]\n", error);
    });
    ArduinoOTA.begin();
    telnetServer.begin();
    Serial.printf("OTA ready  host: %s.local  pw: %s\n", OTA_HOST, OTA_PASS);
    Serial.printf("Telnet: %s:23\n", WiFi.localIP().toString().c_str());
}

// =========================================================================
// TRANSMIT CYCLE — shared by both modes
// =========================================================================
void runTransmitCycle() {
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

    if (usbMode) {
        tprintf("T=%.1f H=%.1f P=%.1f eCO2=%u TVOC=%u AQI=%u\n",
                pkt.sensor1, pkt.sensor2, pkt.sensor3,
                pkt.eco2, pkt.tvoc, pkt.aqi);
    } else {
        tprintf("T=%.1f H=%.1f P=%.1f\n",
                pkt.sensor1, pkt.sensor2, pkt.sensor3);
    }

    radio.powerUp();
    delay(10);
    bool ok = radio.write(&pkt, sizeof(pkt));

    if (ok) {
        tprintf("TX: OK\n");
        if (radio.isAckPayloadAvailable()) {
            float cmd;
            radio.read(&cmd, sizeof(float));
            if (cmd >= 1 && cmd <= 200) {
                sleep_multiplier = (uint8_t)cmd;
                tprintf("ACK: sleep_mult=%u\n", sleep_multiplier);
            }
        }
    } else {
        tprintf("TX: FAILED\n");
    }

    radio.powerDown();
}

// =========================================================================
// SETUP
// =========================================================================
void setup() {
    // ---- DIP switch SW1: read NODE_ID (active-low, pull-up) ----
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
    Serial.println(hasBME ? "BME280: OK" : "WARN: BME280 not found");

    hasENS = ens160.begin();
    if (hasENS) {
        ens160.setMode(ENS160_OPMODE_STD);
        Serial.println("ENS160: OK (STD mode, ~1h warm-up)");
    } else {
        Serial.println("ENS160: not present");
    }

    // ---- Operating mode ----
    usbMode = hasENS;
    Serial.println(usbMode ? "Mode: USB" : "Mode: BATTERY");

    // ---- WiFi + OTA + Telnet (USB mode only) ----
    if (usbMode) {
        setupWiFi();
        if (WiFi.status() == WL_CONNECTED) {
            setupOTA();
        }
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
    radio.setPALevel(RF24_PA_HIGH);
    radio.setRetries(5, 15);
    radio.openWritingPipe(MY_ADDR);

    Serial.println("Setup complete");

    // ---- Battery mode: transmit once then deep sleep ----
    if (!usbMode) {
        runTransmitCycle();
        uint32_t sleep_s = (uint32_t)sleep_multiplier * 8;
        Serial.printf("Deep sleep %u s\n", sleep_s);
        Serial.flush();
        delay(20);
        esp_sleep_enable_timer_wakeup((uint64_t)sleep_s * 1000000ULL);
        esp_deep_sleep_start();
    }
}

// =========================================================================
// LOOP — USB mode only
// OTA and Telnet are serviced every 10 ms; TX fires on sleep_multiplier schedule
// =========================================================================
void loop() {
    ArduinoOTA.handle();

    // Accept new Telnet connection (one client at a time)
    if (telnetServer.hasClient()) {
        if (telnetClient && telnetClient.connected()) telnetClient.stop();
        telnetClient = telnetServer.accept();
        tprintf("=== NODE %d HW2.0  %s ===\n",
                NODE_ID, WiFi.localIP().toString().c_str());
    }

    // Transmit on schedule
    static bool    firstRun = true;
    static uint32_t lastTx  = 0;
    uint32_t sleep_ms = (uint32_t)sleep_multiplier * 8000UL;

    if (firstRun || millis() - lastTx >= sleep_ms) {
        firstRun = false;
        lastTx   = millis();
        runTransmitCycle();
        tprintf("Waiting %u s\n\n", sleep_ms / 1000);
    }

    delay(10);
}
