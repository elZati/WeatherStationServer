/*
 * =========================================================================
 * SENSOR HW 3.0 — DEBUG / BRING-UP FIRMWARE
 * -------------------------------------------------------------------------
 * Target:  Arduino Pro Mini 3.3V / 8MHz (ATmega328P)
 * Upload:  USB-TTL FTDI converter on COM6 @ 57600 baud (bootloader)
 *
 * Tests each cycle (~8 s):
 *   1. I2C scan
 *   2. BME280  — temperature / humidity / pressure
 *   3. ENS160  — eCO2 / TVOC / AQI  (absent = battery-only build, OK)
 *   4. NRF24   — SPI, TX attempt, ARC count, register dump
 *   5. DIP SW1 — 5-position, reads raw + binary NODE_ID
 *   6. Battery — A0 ADC → voltage (verify divider ratio from schematic)
 * =========================================================================
 */

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <RF24.h>

// =========================================================================
// HARDWARE PINS (from schematic)
// =========================================================================
#define NRF_CE   9
#define NRF_CSN  10
// Hardware SPI: SCK=13, MOSI=11, MISO=12 (fixed by ATmega328P)
// Hardware I2C: SDA=A4, SCL=A5 (fixed by ATmega328P)

// DIP switch SW1 — active-low, internal pull-up; other side → GND
#define SW_P1  8    // bit 4  (value 16)
#define SW_P2  7    // bit 3  (value  8)
#define SW_P3  6    // bit 2  (value  4)
#define SW_P4  5    // bit 1  (value  2)
#define SW_P5  4    // bit 0  (value  1)

// Battery ADC — voltage divider midpoint; adjust multiplier once divider confirmed
#define BATT_PIN  A0

// NRF24 TX test address (server pipe 1 — same as production)
const uint64_t TEST_ADDR = 0xABCDABCD01LL;

// =========================================================================
// OBJECTS
// =========================================================================
RF24             radio(NRF_CE, NRF_CSN);
Adafruit_BME280  bme;

// =========================================================================
static void sep() { Serial.println(F("------------------------------------------")); }

// =========================================================================
// 1. I2C SCAN
// =========================================================================
void testI2C() {
    Serial.println(F("=== I2C SCAN ==="));
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print(F("  0x")); Serial.print(addr, HEX);
            if      (addr == 0x76 || addr == 0x77) Serial.print(F("  <- BME280"));
            else if (addr == 0x52 || addr == 0x53) Serial.print(F("  <- ENS160"));
            Serial.println();
            found++;
        }
    }
    if (!found) Serial.println(F("  NONE found — check SDA/SCL wiring"));
}

// =========================================================================
// 2. BME280
// =========================================================================
void testBME280() {
    Serial.println(F("=== BME280 ==="));
    if (!bme.begin(0x76)) {
        Serial.println(F("  FAIL: not found at 0x76"));
        return;
    }
    float t = bme.readTemperature();
    float h = bme.readHumidity();
    float p = bme.readPressure() / 100.0f;
    Serial.print(F("  T=")); Serial.print(t, 1); Serial.print(F("C  "));
    Serial.print(F("H=")); Serial.print(h, 1); Serial.print(F("%  "));
    Serial.print(F("P=")); Serial.print(p, 1); Serial.println(F("hPa"));
    bool ok = (t > -40 && t < 85 && h >= 0 && h <= 100 && p > 800 && p < 1200);
    Serial.println(ok ? F("  PASS") : F("  WARN: readings out of valid range"));
}

// =========================================================================
// 3. ENS160  — presence check via raw I2C (ScioSense lib is ESP32-only)
// =========================================================================
void testENS160() {
    Serial.println(F("=== ENS160 ==="));
    Wire.beginTransmission(0x52);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
        Serial.print(F("  not found at 0x52 (err=")); Serial.print(err);
        Serial.println(F(") — OK for battery-only build"));
        return;
    }
    // Read PART_ID register (0x00) — should be 0x0160 for ENS160
    Wire.beginTransmission(0x52);
    Wire.write(0x00);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x52, (uint8_t)2);
    uint8_t lo = Wire.read();
    uint8_t hi = Wire.read();
    uint16_t partId = ((uint16_t)hi << 8) | lo;
    Serial.print(F("  Found at 0x52 — PART_ID=0x")); Serial.print(partId, HEX);
    Serial.println(partId == 0x0160 ? F("  PASS") : F("  WARN: unexpected PART_ID"));
}

// =========================================================================
// 4. NRF24
// =========================================================================
void testNRF24() {
    Serial.println(F("=== NRF24 ==="));
    if (!radio.begin()) {
        Serial.println(F("  FAIL: begin() false — check SPI wiring (CE=D9, CSN=D10)"));
        return;
    }
    if (!radio.isChipConnected()) {
        Serial.println(F("  FAIL: isChipConnected() false — CE or CSN issue"));
        return;
    }
    Serial.println(F("  SPI: OK"));

    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.setAutoAck(true);
    radio.enableDynamicPayloads();
    radio.enableAckPayload();
    radio.setPALevel(RF24_PA_HIGH);
    radio.setRetries(5, 15);
    radio.openWritingPipe(TEST_ADDR);

    uint8_t buf[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    bool ok = radio.write(buf, 4);
    uint8_t arc = radio.getARC();

    Serial.print(F("  TX: "));
    Serial.print(ok ? F("ACK received") : F("no ACK (expected if no server nearby)"));
    Serial.print(F("  ARC="));
    Serial.print(arc);
    if      (arc == 0)  Serial.println(F("  <- 0=CE/SPI issue"));
    else if (arc == 15) Serial.println(F("  <- 15=RF output failure or no receiver"));
    else                Serial.println();

    radio.printDetails();
    radio.powerDown();
    Serial.println(F("  Radio powered down"));
}

// =========================================================================
// 5. DIP SWITCH
// =========================================================================
void testDIPSwitch() {
    Serial.println(F("=== DIP SWITCH SW1 ==="));
    const uint8_t pins[5]   = {SW_P1, SW_P2, SW_P3, SW_P4, SW_P5};
    const char*   labels[5] = {"P1(D8)", "P2(D7)", "P3(D6)", "P4(D5)", "P5(D4)"};

    uint8_t nodeId = 0;
    for (int i = 0; i < 5; i++) {
        bool on = !digitalRead(pins[i]);
        Serial.print(F("  ")); Serial.print(labels[i]);
        Serial.println(on ? F(": ON") : F(": OFF"));
        if (on) nodeId = (i + 1);   // one-hot: last ON switch wins, 1-5
    }
    Serial.print(F("  NODE_ID (one-hot P1=1..P5=5) = ")); Serial.println(nodeId);
    if (nodeId == 0) Serial.println(F("  WARN: no switch ON — set exactly one switch for NODE_ID"));
}

// =========================================================================
// 6. BATTERY ADC
// =========================================================================
void testBattery() {
    Serial.println(F("=== BATTERY ==="));
    int raw = analogRead(BATT_PIN);
    // Divider ratio TBD — using 0.5 (two equal resistors) as placeholder
    float voltage = (raw / 1023.0f) * 3.3f * 2.0f;
    Serial.print(F("  A0 raw=")); Serial.print(raw);
    Serial.print(F("  ~")); Serial.print(voltage, 2);
    Serial.println(F("V  (verify voltage divider ratio from schematic)"));
}

// =========================================================================
// SETUP
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== SENSOR HW3 DEBUG FIRMWARE ==="));
    Serial.println(F("Arduino Pro Mini 3.3V/8MHz"));

    pinMode(SW_P1, INPUT_PULLUP);
    pinMode(SW_P2, INPUT_PULLUP);
    pinMode(SW_P3, INPUT_PULLUP);
    pinMode(SW_P4, INPUT_PULLUP);
    pinMode(SW_P5, INPUT_PULLUP);

    Wire.begin();
}

// =========================================================================
// LOOP
// =========================================================================
void loop() {
    Serial.println();
    sep(); testI2C();
    sep(); testBME280();
    sep(); testENS160();
    sep(); testNRF24();
    sep(); testDIPSwitch();
    sep(); testBattery();
    sep();
    Serial.println(F("Waiting 8s...\n"));
    delay(8000);
}
