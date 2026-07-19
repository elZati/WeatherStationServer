/*
 * =========================================================================
 * SENSOR HW 3.0 FIRMWARE (v1.0)
 * -------------------------------------------------------------------------
 * Target:   Arduino Pro Mini 3.3V / 8MHz (ATmega328P)
 * Radio:    NRF24L01+PA (E01-ML01DPA_TH) — CE=D9, CSN=D10
 *
 * SW1 DIP switch (active-low, pull-up; other side of all switches → GND):
 *   P1 (D8) = NODE_ID 1  ─┐
 *   P2 (D7) = NODE_ID 2   │
 *   P3 (D6) = NODE_ID 3   ├─ one-hot: exactly one switch ON
 *   P4 (D5) = NODE_ID 4   │
 *   P5 (D4) = NODE_ID 5  ─┘
 *
 * NOTE: DIP switch reading disabled — NODE_ID hardcoded below.
 *       Enable switch reading once SW1 is soldered and re-flash via USB.
 *
 * Sleep: ATmega328P power-down via WDT (8s per cycle).
 *        sleep_multiplier × 8s total (default 15 = 120s).
 *        sleep_multiplier updated by server ACK payload.
 *
 * Protocol: 25-byte SensorPayloadV2 (server identifies by payload size:
 *           25 b = HW 2.0/3.0, 20 b = HW 1.x legacy).
 * =========================================================================
 */

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <RF24.h>
#include <LowPower.h>

// =========================================================================
// HARDWARE PINS
// =========================================================================
#define NRF_CE   9
#define NRF_CSN  10
// Hardware SPI: SCK=13, MOSI=11, MISO=12
// Hardware I2C: SDA=A4, SCL=A5

// DIP switch SW1 (active-low, internal pull-up)
#define SW_P1  8    // NODE_ID 1
#define SW_P2  7    // NODE_ID 2
#define SW_P3  6    // NODE_ID 3
#define SW_P4  5    // NODE_ID 4
#define SW_P5  4    // NODE_ID 5

// Bandgap calibration constant: 1100 mV * 1023 * 1000
// Adjust if VCC reads wrong vs multimeter: constant = measured_VCC_mV * ADC_result
#define BANDGAP_CAL  1125300UL

// =========================================================================
// NODE CONFIG — hardcoded until SW1 is soldered
// =========================================================================
#define HARDCODED_NODE_ID  0   // 0 = read DIP switch SW1; 1-5 = override

// =========================================================================
// 25-BYTE PAYLOAD — keep in sync with SensorPayloadV2 in Server_v15.cpp
// =========================================================================
#pragma pack(push, 1)
struct SensorPayloadV2 {
    int32_t  nodeID;   // 4 bytes
    float    sensor1;  // 4 bytes — temperature °C
    float    sensor2;  // 4 bytes — humidity %RH
    float    sensor3;  // 4 bytes — pressure hPa
    float    sensor4;  // 4 bytes — battery voltage V
    uint16_t eco2;     // 2 bytes — 0 (no ENS160)
    uint16_t tvoc;     // 2 bytes — 0 (no ENS160)
    uint8_t  aqi;      // 1 byte  — 0 (no ENS160)
};                     // total: 25 bytes
#pragma pack(pop)

// =========================================================================
// ADDRESSING
// =========================================================================
const uint64_t BASE_ADDR = 0xABCDABCD00LL;

// =========================================================================
// GLOBALS
// =========================================================================
uint8_t  sleep_multiplier = 15;   // 15 × 8s = 120s
int32_t  NODE_ID = 0;
uint64_t MY_ADDR = 0;

RF24            radio(NRF_CE, NRF_CSN);
Adafruit_BME280 bme;

// =========================================================================
// NODE ID
// =========================================================================
int32_t readNodeId() {
#if HARDCODED_NODE_ID > 0
    return HARDCODED_NODE_ID;
#else
    const uint8_t pins[5] = {SW_P1, SW_P2, SW_P3, SW_P4, SW_P5};
    for (int i = 0; i < 5; i++) {
        if (!digitalRead(pins[i])) return i + 1;
    }
    return 0;
#endif
}

// =========================================================================
// BATTERY / VCC VOLTAGE — internal 1.1V bandgap trick, no divider needed
// Reports ~3.3V when healthy; drops below 3.3V when LDO can't regulate.
// =========================================================================
float readBattery() {
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);  // 1.1V ref vs VCC
    delay(4);                                                    // bandgap settle
    ADCSRA |= _BV(ADSC);
    while (bit_is_set(ADCSRA, ADSC));
    return BANDGAP_CAL / (float)ADC / 1000.0f;
}

// =========================================================================
// WDT SLEEP
// =========================================================================
void sleepSeconds(uint16_t seconds) {
    uint8_t cycles = seconds / 8;
    for (uint8_t i = 0; i < cycles; i++) {
        LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    }
}

// =========================================================================
// TRANSMIT CYCLE
// =========================================================================
void runTransmitCycle() {
    SensorPayloadV2 pkt = {};
    pkt.nodeID  = NODE_ID;
    bme.takeForcedMeasurement();
    pkt.sensor1 = bme.readTemperature();
    pkt.sensor2 = bme.readHumidity();
    pkt.sensor3 = bme.readPressure() / 100.0f;
    pkt.sensor4 = readBattery();

    Serial.print(F("T=")); Serial.print(pkt.sensor1, 1);
    Serial.print(F(" H=")); Serial.print(pkt.sensor2, 1);
    Serial.print(F(" P=")); Serial.print(pkt.sensor3, 1);
    Serial.print(F(" V=")); Serial.println(pkt.sensor4, 2);

    radio.powerUp();
    delay(100);
    radio.flush_tx();
    bool ok = radio.write(&pkt, sizeof(pkt));
    uint8_t arc = radio.getARC();

    if (ok) {
        Serial.print(F("TX: OK  ARC=")); Serial.println(arc);
        if (radio.isAckPayloadAvailable()) {
            float cmd;
            radio.read(&cmd, sizeof(float));
            if (cmd >= 1 && cmd <= 200) {
                sleep_multiplier = (uint8_t)cmd;
                Serial.print(F("ACK: sleep_mult=")); Serial.println(sleep_multiplier);
            }
        }
    } else {
        Serial.print(F("TX: FAILED  ARC=")); Serial.println(arc);
    }

    radio.powerDown();
}

// =========================================================================
// SETUP
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(100);

    pinMode(SW_P1, INPUT_PULLUP);
    pinMode(SW_P2, INPUT_PULLUP);
    pinMode(SW_P3, INPUT_PULLUP);
    pinMode(SW_P4, INPUT_PULLUP);
    pinMode(SW_P5, INPUT_PULLUP);

    // Unused pins pulled up to prevent floating
    pinMode(2,  INPUT_PULLUP);
    pinMode(3,  INPUT_PULLUP);
    pinMode(A0, INPUT_PULLUP);
    pinMode(A1, INPUT_PULLUP);
    pinMode(A2, INPUT_PULLUP);
    pinMode(A3, INPUT_PULLUP);

    delay(10);

    NODE_ID = readNodeId();
    MY_ADDR = BASE_ADDR + NODE_ID;

    Serial.print(F("\n=== NODE ")); Serial.print(NODE_ID);
    Serial.println(F(" HW3.0 START ==="));

    if (NODE_ID < 1 || NODE_ID > 5) {
        Serial.println(F("CRITICAL: invalid NODE_ID — halting"));
        while (1) LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
    }

    Wire.begin();
    if (!bme.begin(0x76)) {
        Serial.println(F("CRITICAL: BME280 not found — halting"));
        while (1) LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
    }
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X2,
                    Adafruit_BME280::SAMPLING_X2,
                    Adafruit_BME280::SAMPLING_X2,
                    Adafruit_BME280::FILTER_OFF,
                    Adafruit_BME280::STANDBY_MS_0_5);
    Serial.println(F("BME280: OK"));

    if (!radio.begin()) {
        Serial.println(F("CRITICAL: radio not found — halting"));
        while (1) LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
    }
    radio.setChannel(76);
    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.setAutoAck(true);
    radio.enableDynamicPayloads();
    radio.enableAckPayload();
    radio.setPALevel(RF24_PA_HIGH);
    radio.setRetries(15, 15);
    radio.stopListening();
    radio.openWritingPipe(MY_ADDR);
    radio.powerDown();
    Serial.println(F("NRF24: OK"));
    Serial.println(F("Setup complete"));
}

// =========================================================================
// LOOP
// =========================================================================
void loop() {
    runTransmitCycle();

    uint16_t sleep_s = (uint16_t)sleep_multiplier * 8;
    Serial.print(F("Sleep ")); Serial.print(sleep_s); Serial.println(F("s"));
    Serial.flush();

    sleepSeconds(sleep_s);
}
