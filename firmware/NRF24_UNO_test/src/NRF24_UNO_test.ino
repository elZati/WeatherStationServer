/*
 * =========================================================================
 * NRF24 UNO TEST NODE — NODE ID 4
 * -------------------------------------------------------------------------
 * Target:  Arduino Uno (ATmega328P, 5V/16MHz)
 * Radio:   NRF24L01+ — CE=D9, CSN=D10
 *
 * Wiring:
 *   NRF24 VCC  → Uno 3.3V
 *   NRF24 GND  → Uno GND
 *   NRF24 CE   → D9
 *   NRF24 CSN  → D10
 *   NRF24 SCK  → D13
 *   NRF24 MOSI → D11
 *   NRF24 MISO → D12
 *
 * Sends V2 25-byte payload every 10 s — same format as HW2/HW3 nodes.
 * =========================================================================
 */

#include <SPI.h>
#include <RF24.h>

#define NRF_CE   9
#define NRF_CSN  10

#define NODE_ID  3

RF24 radio(NRF_CE, NRF_CSN);
const uint64_t MY_ADDR = 0xABCDABCD00LL + NODE_ID;

// =========================================================================
// 25-BYTE PAYLOAD — keep in sync with SensorPayloadV2 in Server_v15.cpp
// =========================================================================
#pragma pack(push, 1)
struct SensorPayloadV2 {
    int32_t  nodeID;   // 4 bytes
    float    sensor1;  // 4 bytes — temperature °C
    float    sensor2;  // 4 bytes — humidity %RH
    float    sensor3;  // 4 bytes — pressure hPa
    float    sensor4;  // 4 bytes — voltage V  (0 = no sensor)
    uint16_t eco2;     // 2 bytes — eCO2 ppm   (0 = no ENS160)
    uint16_t tvoc;     // 2 bytes — TVOC ppb   (0 = no ENS160)
    uint8_t  aqi;      // 1 byte  — AQI 1-5    (0 = no ENS160)
};                     // total: 25 bytes
#pragma pack(pop)

uint8_t sleep_multiplier = 10;
uint8_t fail_count = 0;

// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== UNO NRF24 TEST — NODE 3 ==="));

    // Raw SPI probe: read NRF24 CONFIG register (addr 0x00); default = 0x08
    // 0xFF/0x00 = SPI dead; 0x08 = chip present but begin() failing
    pinMode(NRF_CSN, OUTPUT);
    digitalWrite(NRF_CSN, HIGH);
    SPI.begin();
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(NRF_CSN, LOW);
    SPI.transfer(0x00);                   // read register 0
    uint8_t spi_val = SPI.transfer(0xFF); // dummy byte → get register value
    digitalWrite(NRF_CSN, HIGH);
    SPI.endTransaction();
    Serial.print(F("SPI probe CONFIG reg=0x")); Serial.println(spi_val, HEX);
    // 0x08 = chip seen; 0x00 or 0xFF = no SPI contact

    for (uint8_t i = 0; i < 5; i++) {
        if (radio.begin()) break;
        Serial.print(F("radio.begin() failed, retry "));
        Serial.println(i + 1);
        delay(200);
        if (i == 4) {
            Serial.println(F("FATAL: radio not found — check wiring (CE=D9, CSN=D10)"));
            while (1);
        }
    }

    radio.setChannel(76);
    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.setAutoAck(true);
    radio.enableDynamicPayloads();
    radio.enableAckPayload();
    radio.setPALevel(RF24_PA_LOW);
    radio.setRetries(15, 15);
    radio.stopListening();
    radio.openWritingPipe(MY_ADDR);

    Serial.println(F("Radio OK — transmitting every 10 s"));
}

// =========================================================================
void loop() {
    SensorPayloadV2 pkt = {};
    pkt.nodeID  = NODE_ID;
    pkt.sensor1 = 25.0f;    // fixed test values — replace with real sensors
    pkt.sensor2 = 50.0f;
    pkt.sensor3 = 1013.0f;
    pkt.sensor4 = 5.0f;     // Uno runs on USB 5V

    Serial.print(F("TX NODE ")); Serial.print(NODE_ID);
    Serial.print(F("  ")); Serial.print(sizeof(pkt)); Serial.print(F("B ... "));

    radio.setRetries(15, 15);
    radio.flush_rx();
    radio.flush_tx();
    delay(5);
    bool ok = radio.write(&pkt, sizeof(pkt));
    uint8_t arc = radio.getARC();

    if (ok) {
        Serial.print(F("OK  ARC=")); Serial.println(arc);
        if (radio.isAckPayloadAvailable()) {
            float cmd;
            radio.read(&cmd, sizeof(float));
            if (cmd >= 1 && cmd <= 200) {
                sleep_multiplier = (uint8_t)cmd;
                Serial.print(F("ACK: sleep_mult=")); Serial.println(sleep_multiplier);
            }
        }
    } else {
        Serial.print(F("FAILED  ARC="));
        Serial.print(arc);
        if      (arc == 0)  Serial.print(F("  (CE/SPI)"));
        else if (arc == 15) Serial.print(F("  (no ACK)"));
        Serial.println();
        fail_count++;
        if (fail_count >= 2) {
            Serial.println(F("Reinit radio..."));
            radio.begin();
            radio.setChannel(76);
            radio.setDataRate(RF24_250KBPS);
            radio.setCRCLength(RF24_CRC_16);
            radio.setAutoAck(true);
            radio.enableDynamicPayloads();
            radio.enableAckPayload();
            radio.setPALevel(RF24_PA_LOW);
            radio.setRetries(15, 15);
            radio.stopListening();
            radio.openWritingPipe(MY_ADDR);
            fail_count = 0;
        }
    }

    delay(10000);
}
