/*
 * =========================================================================
 * SENSOR HW 3.0 FIRMWARE (v1.0)
 * -------------------------------------------------------------------------
 * Target:   Arduino Pro Mini (ATmega328P, 3.3 V / 8 MHz)
 * Radio:    NRF24L01+PA — CE=D9, CSN=D10
 *
 * SW1 DIP switch (active-low, pull-up; other side of all switches → GND):
 *   P1 (D8) = NODE_ID bit 2 (value 4) ─┐
 *   P2 (D7) = NODE_ID bit 1 (value 2)  ├─ NODE_ID 1–6
 *   P3 (D6) = NODE_ID bit 0 (value 1) ─┘
 *   P4 (D5) = PA bit 1 (value 2) ─┐  0=MIN  1=LOW
 *   P5 (D4) = PA bit 0 (value 1) ─┘  2=HIGH 3=MAX
 *
 * Operating mode is auto-detected at boot by I2C scan:
 *
 *   BATTERY mode — BME280 only (no ENS160 found)
 *     · WDT SLEEP_MODE_PWR_DOWN between transmissions
 *     · BOD disabled during sleep (~20 µA saving)
 *     · ADC disabled during sleep
 *     · BME280 in forced mode (single shot → sleep between readings)
 *     · Radio powered down between transmissions
 *     · sensor4 = battery voltage (AVR bandgap)
 *
 *   USB mode — BME280 + ENS160 found
 *     · delay() between transmissions — MCU stays awake so ENS160 keeps
 *       its gas-sensor baseline (deep sleep would reset it every cycle)
 *     · BME280 in normal mode; temp/humidity fed to ENS160 each cycle
 *     · sensor4 = 0.0 (USB powered, no battery circuit)
 *
 * Protocol: 25-byte SensorPayloadV2 (same as HW 2.0; server identifies
 *           HW version by dynamic payload size: 25 b = v2, 20 b = legacy).
 *
 * Libraries:
 *   adafruit/Adafruit BME280 Library
 *   adafruit/Adafruit Unified Sensor
 *   sciosense/ScioSense_ENS160
 *   tmrh20/RF24
 * =========================================================================
 */

#include <SPI.h>
#include <Wire.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <ScioSense_ENS160.h>
#include <RF24.h>

// =========================================================================
// DIP SWITCH PINS — SW1 (active-low, internal pull-up)
// =========================================================================
#define SW_P1   8   // NODE_ID bit 2 (value 4)
#define SW_P2   7   // NODE_ID bit 1 (value 2)
#define SW_P3   6   // NODE_ID bit 0 (value 1)
#define SW_P4   5   // PA level bit 1 (value 2)
#define SW_P5   4   // PA level bit 0 (value 1)

// =========================================================================
// HARDWARE PINS
// =========================================================================
#define NRF_CE   9
#define NRF_CSN  10
// SPI  : D11=MOSI  D12=MISO  D13=SCK  (hardware SPI, implicit)
// I2C  : A4=SDA    A5=SCL              (Wire.begin() defaults)

// =========================================================================
// 25-BYTE PAYLOAD — keep in sync with SensorPayloadV2 in Server_v15.cpp
// =========================================================================
#pragma pack(push, 1)
struct SensorPayloadV2 {
    int32_t  nodeID;   // 4 bytes
    float    sensor1;  // 4 bytes — temperature °C
    float    sensor2;  // 4 bytes — humidity %RH
    float    sensor3;  // 4 bytes — pressure hPa
    float    sensor4;  // 4 bytes — battery V (battery mode) / 0.0 (USB mode)
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
// =========================================================================
RF24             radio(NRF_CE, NRF_CSN);
Adafruit_BME280  bme;
ScioSense_ENS160 ens160;

int32_t  NODE_ID          = 0;
uint64_t MY_ADDR          = 0;
bool     hasBME           = false;
bool     hasENS           = false;
bool     usbMode          = false;   // true when ENS160 detected
uint8_t  sleep_multiplier = 5;       // 5 × 8 s = 40 s default

volatile bool wdtFired = false;

ISR(WDT_vect) { wdtFired = true; }

// =========================================================================
// BATTERY-MODE SLEEP
// Disables ADC and BOD before sleeping to minimize current draw.
// sleep_bod_disable() must be immediately before sleep_cpu() — the BODS bit
// is hardware-cleared after 3 clock cycles, so no code can go between them.
// =========================================================================
void enterBatterySleep() {
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    byte old_ADCSRA = ADCSRA;
    ADCSRA = 0;
    sleep_bod_disable();
    sleep_cpu();          // <-- wakes here on WDT interrupt
    sleep_disable();
    ADCSRA = old_ADCSRA;
}

// =========================================================================
// BATTERY VOLTAGE — ATmega328P VCC via internal 1.1 V bandgap reference
// =========================================================================
float getBatteryVoltage() {
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
    delay(2);
    ADCSRA |= _BV(ADSC);
    while (bit_is_set(ADCSRA, ADSC));
    uint8_t low = ADCL, high = ADCH;
    long result = (high << 8) | low;
    result = 1125300L / result;
    return (float)result / 1000.0;
}

// =========================================================================
// WDT — 8-second interval, interrupt mode only (no reset)
// =========================================================================
void setup_watchdog() {
    noInterrupts();
    MCUSR &= ~(1 << WDRF);
    WDTCSR |= (1 << WDCE) | (1 << WDE);
    WDTCSR = (1 << WDP0) | (1 << WDP3);
    WDTCSR |= (1 << WDIE);
    interrupts();
}

// =========================================================================
// I2C SCANNER — prints found devices; returns device count
// =========================================================================
uint8_t scanI2C() {
    Serial.println(F("I2C scan:"));
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print(F("  0x"));
            if (addr < 16) Serial.print('0');
            Serial.print(addr, HEX);
            if      (addr == 0x76 || addr == 0x77) Serial.print(F("  BME280"));
            else if (addr == 0x52 || addr == 0x53) Serial.print(F("  ENS160"));
            Serial.println();
            found++;
        }
    }
    if (!found) Serial.println(F("  none"));
    return found;
}

// =========================================================================
// TRANSMIT CYCLE — shared by both modes
// =========================================================================
void runTransmitCycle() {
    SensorPayloadV2 pkt = {};
    pkt.nodeID  = NODE_ID;
    pkt.sensor4 = usbMode ? 0.0f : getBatteryVoltage();

    if (hasBME) {
        if (!usbMode) bme.takeForcedMeasurement();   // battery: single-shot then BME sleeps
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

    Serial.print(F("T="));     Serial.print(pkt.sensor1);
    Serial.print(F(" H="));    Serial.print(pkt.sensor2);
    Serial.print(F(" P="));    Serial.print(pkt.sensor3);
    if (usbMode) {
        Serial.print(F(" eCO2=")); Serial.print(pkt.eco2);
        Serial.print(F(" TVOC=")); Serial.print(pkt.tvoc);
        Serial.print(F(" AQI="));  Serial.print(pkt.aqi);
    } else {
        Serial.print(F(" V="));    Serial.print(pkt.sensor4);
    }
    Serial.println();

    radio.powerUp();
    delay(10);
    bool ok = radio.write(&pkt, sizeof(pkt));

    if (ok) {
        Serial.println(F("TX: OK"));
        if (radio.isAckPayloadAvailable()) {
            float cmd;
            radio.read(&cmd, sizeof(float));
            if (cmd >= 1 && cmd <= 200) {
                sleep_multiplier = (uint8_t)cmd;
                Serial.print(F("ACK: sleep_mult=")); Serial.println(sleep_multiplier);
            }
        }
    } else {
        Serial.println(F("TX: FAILED"));
    }

    radio.powerDown();
}

// =========================================================================
// SETUP
// =========================================================================
void setup() {
    // ---- DIP switch ----
    pinMode(SW_P1, INPUT_PULLUP);
    pinMode(SW_P2, INPUT_PULLUP);
    pinMode(SW_P3, INPUT_PULLUP);
    pinMode(SW_P4, INPUT_PULLUP);
    pinMode(SW_P5, INPUT_PULLUP);
    delay(10);

    NODE_ID = ((!digitalRead(SW_P1)) << 2) |
              ((!digitalRead(SW_P2)) << 1) |
              ((!digitalRead(SW_P3)) << 0);

    uint8_t pa_index = ((!digitalRead(SW_P4)) << 1) |
                       ((!digitalRead(SW_P5)) << 0);

    MY_ADDR = BASE_ADDR + NODE_ID;

    Serial.begin(57600);
    delay(100);
    Serial.println(F("\n=== HW3.0 START ==="));
    Serial.print(F("NODE_ID=")); Serial.print(NODE_ID);
    Serial.print(F("  PA="));   Serial.println(pa_index);

    if (NODE_ID < 1 || NODE_ID > 6) {
        Serial.print(F("CRITICAL: Invalid NODE_ID="));
        Serial.print(NODE_ID);
        Serial.println(F(" — set SW1 P1-P3 to 1-6 and reboot"));
        while (1);
    }

    // ---- I2C scan + sensor init ----
    Wire.begin();
    scanI2C();

    hasBME = bme.begin(0x76, &Wire);
    if (hasBME) {
        Serial.println(F("BME280: OK"));
    } else {
        Serial.println(F("WARN: BME280 not found"));
    }

    hasENS = ens160.begin();
    if (hasENS) {
        ens160.setMode(ENS160_OPMODE_STD);
        Serial.println(F("ENS160: OK (STD mode, ~1h warm-up)"));
    } else {
        Serial.println(F("ENS160: not present"));
    }

    // ---- Operating mode ----
    usbMode = hasENS;

    if (usbMode) {
        Serial.println(F("Mode: USB — delay() sleep, ENS160 continuous"));
    } else {
        // Forced mode: BME280 does one measurement then returns to sleep,
        // drawing only ~0.1 µA until the next takeForcedMeasurement() call.
        bme.setSampling(
            Adafruit_BME280::MODE_FORCED,
            Adafruit_BME280::SAMPLING_X1,
            Adafruit_BME280::SAMPLING_X1,
            Adafruit_BME280::SAMPLING_X1,
            Adafruit_BME280::FILTER_OFF
        );
        Serial.println(F("Mode: BATTERY — WDT sleep, BOD off, BME280 forced"));
    }

    // ---- NRF24 ----
    if (!radio.begin()) {
        Serial.println(F("CRITICAL: Radio not found — halting"));
        while (1);
    }

    const rf24_pa_dbm_e pa_levels[] = {
        RF24_PA_MIN, RF24_PA_LOW, RF24_PA_HIGH, RF24_PA_MAX
    };

    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.setAutoAck(true);
    radio.enableDynamicPayloads();
    radio.enableAckPayload();
    radio.setPALevel(pa_levels[pa_index]);
    radio.setRetries(5, 15);
    radio.openWritingPipe(MY_ADDR);
    radio.powerDown();

    Serial.println(F("Ready"));
    Serial.flush();

    if (!usbMode) setup_watchdog();
}

// =========================================================================
// LOOP
// =========================================================================
void loop() {
    if (usbMode) {
        // USB mode: MCU stays awake; delay() preserves ENS160 baseline
        runTransmitCycle();
        uint32_t wait_ms = (uint32_t)sleep_multiplier * 8000UL;
        Serial.print(F("Waiting ")); Serial.print(wait_ms / 1000); Serial.println(F(" s"));
        Serial.flush();
        delay(wait_ms);

    } else {
        // Battery mode: WDT-driven; transmit once per sleep_multiplier × 8 s
        static bool    first_run = true;
        static uint8_t wdt_count = 0;

        if (first_run) {
            first_run = false;
        } else {
            if (!wdtFired)              { enterBatterySleep(); return; }
            wdtFired = false;
            if (++wdt_count < sleep_multiplier) { enterBatterySleep(); return; }
            wdt_count = 0;
        }

        runTransmitCycle();

        Serial.print(F("Sleeping ")); Serial.print((uint32_t)sleep_multiplier * 8); Serial.println(F(" s"));
        Serial.flush();
        enterBatterySleep();
    }
}
