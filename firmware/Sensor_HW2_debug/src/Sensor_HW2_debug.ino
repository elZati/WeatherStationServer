/*
 * =========================================================================
 * SENSOR HW 2.0 — DIAGNOSTIC FIRMWARE
 * =========================================================================
 * Tests each hardware component independently and reports PASS / WARN / FAIL.
 * Loops every 8 s so results update live as you probe the board.
 *
 * NODE_ID is hardcoded below — SW1 DIP switch is NOT read.
 *
 * Target:  ESP32-C3 SuperMini (HW-466AB)
 * Upload:  pio run -t upload  (from firmware/Sensor_HW2_debug/)
 * Monitor: pio device monitor  (or any 115200-baud terminal on COM port)
 * =========================================================================
 */

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <ScioSense_ENS160.h>
#include <RF24.h>

// =========================================================================
// !! HARDCODED NODE ID — change before flashing production firmware !!
// =========================================================================
#define DEBUG_NODE_ID  4

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

// =========================================================================
// OBJECTS
// =========================================================================
SPIClass         spiNrf(FSPI);
RF24             radio(NRF_CE, NRF_CSN);
Adafruit_BME280  bme;
ScioSense_ENS160 ens160;

// =========================================================================
// HELPERS
// =========================================================================
static uint32_t cycle = 0;

void pass(const char *msg)  { Serial.printf("  %-22s [PASS]\n", msg); }
void warn(const char *msg)  { Serial.printf("  %-22s [WARN]\n", msg); }
void fail(const char *msg)  { Serial.printf("  %-22s [FAIL] <---\n", msg); }

void section(const char *title) {
    Serial.println();
    Serial.printf("[%s]\n", title);
}

// =========================================================================
// TEST: I2C BUS SCAN
// Returns bitmask: bit0=BME280 found, bit1=ENS160 found
// =========================================================================
uint8_t testI2C() {
    section("I2C BUS SCAN  (SDA=GPIO4  SCL=GPIO5)");

    uint8_t found = 0;
    uint8_t devCount = 0;

    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            devCount++;
            const char *label = "";
            if      (addr == 0x76 || addr == 0x77) { label = "BME280"; found |= 0x01; }
            else if (addr == 0x52 || addr == 0x53) { label = "ENS160"; found |= 0x02; }
            Serial.printf("  0x%02X  %-8s  DETECTED\n", addr, label);
        }
    }

    if (devCount == 0) {
        fail("No I2C devices found — check SDA/SCL wiring and pull-ups");
    } else {
        Serial.printf("  %u device(s) found\n", devCount);
        if (!(found & 0x01)) warn("BME280 not on bus (expected 0x76)");
        if (!(found & 0x02)) warn("ENS160 not on bus (expected 0x52)");
        if (found == 0x03)   pass("Both sensors on bus");
    }
    return found;
}

// =========================================================================
// TEST: BME280
// =========================================================================
void testBME280(bool present) {
    section("BME280  (Temp / Humidity / Pressure)");

    if (!present) {
        fail("Skipped — not detected on I2C bus");
        return;
    }

    if (!bme.begin(0x76, &Wire)) {
        fail("begin(0x76) returned false — wrong address or wiring fault");
        return;
    }
    pass("begin()");

    float temp  = bme.readTemperature();
    float hum   = bme.readHumidity();
    float press = bme.readPressure() / 100.0f;

    Serial.printf("  Temperature: %6.2f C\n",    temp);
    Serial.printf("  Humidity:    %6.2f %%RH\n", hum);
    Serial.printf("  Pressure:    %7.2f hPa\n",  press);

    bool ok = true;
    if (temp  < -40.0f || temp  > 85.0f)   { warn("Temperature out of sensor range (-40..85 C)");   ok = false; }
    if (hum   < 0.0f   || hum   > 100.0f)  { warn("Humidity out of range (0..100 %RH)");             ok = false; }
    if (press < 300.0f || press > 1100.0f)  { warn("Pressure out of range (300..1100 hPa)");          ok = false; }
    if (temp  == 0.0f  && hum == 0.0f)      { fail("All zeros — sensor may be unresponsive");         ok = false; }

    if (ok) pass("Values in range");
}

// =========================================================================
// TEST: ENS160
// =========================================================================
void testENS160(bool present) {
    section("ENS160  (eCO2 / TVOC / AQI)");

    if (!present) {
        fail("Skipped — not detected on I2C bus");
        return;
    }

    if (!ens160.begin()) {
        fail("begin() returned false — wrong address or wiring fault");
        return;
    }
    pass("begin()");

    if (!ens160.setMode(ENS160_OPMODE_STD)) {
        warn("setMode(STD) failed — check ENS160 power and I2C");
    } else {
        pass("setMode(STD)");
    }

    bool newData = ens160.measure(true);
    uint8_t  aqi  = ens160.getAQI();
    uint16_t eco2 = ens160.geteCO2();
    uint16_t tvoc = ens160.getTVOC();

    Serial.printf("  New data:  %s\n",   newData ? "yes" : "no (sensor may need more time)");
    Serial.printf("  AQI:       %u  (1=excellent..5=unhealthy; 0=warming up)\n", aqi);
    Serial.printf("  eCO2:      %u ppm\n", eco2);
    Serial.printf("  TVOC:      %u ppb\n", tvoc);

    if (!newData) {
        warn("No new data yet — sensor initialising");
    } else if (aqi == 0) {
        warn("AQI=0: warming up (~1h for stable baseline) — sensor is responding");
    } else {
        pass("Valid readings");
    }
}

// =========================================================================
// TEST: NRF24L01+PA
// =========================================================================
void testNRF24() {
    section("NRF24L01+PA  (CE=GPIO0  CSN=GPIO1  SPI=FSPI)");

    spiNrf.begin(NRF_SCK, NRF_MISO, NRF_MOSI, NRF_CSN);

    if (!radio.begin(&spiNrf)) {
        fail("begin() failed — check SPI wiring (SCK/MOSI/MISO/CSN/CE)");
        fail("Also check VCC 3.3V and decoupling caps (47uF + 100nF on VCC/GND)");
        return;
    }
    pass("begin()  SPI communication OK");

    if (radio.isChipConnected()) {
        pass("isChipConnected()");
    } else {
        fail("isChipConnected() false — SPI present but chip not responding");
    }

    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.setPALevel(RF24_PA_MAX);
    radio.setRetries(15, 15);
    radio.enableDynamicPayloads();
    radio.enableAckPayload();

    const uint64_t BASE_ADDR = 0xABCDABCD00LL;
    const uint64_t TX_ADDR   = BASE_ADDR + DEBUG_NODE_ID;
    radio.openWritingPipe(TX_ADDR);

    uint8_t chan = radio.getChannel();
    Serial.printf("  Data rate:  250 KBPS\n");
    Serial.printf("  PA level:   MAX\n");
    Serial.printf("  RF channel: %u  (2.%03u GHz)\n", chan, 400 + chan);
    Serial.printf("  TX address: 0x%010llX  (NODE_ID=%d)\n", TX_ADDR, DEBUG_NODE_ID);

    radio.powerUp();
    delay(10);
    uint8_t testPkt[4] = { 0xDB, 0x6E, 0x00, (uint8_t)DEBUG_NODE_ID };
    bool sent = radio.write(testPkt, sizeof(testPkt));

    // ARC = auto retry count used. If 0 → CE never fired (wiring/pin issue).
    // If = max retries → radio fired but got no ACK (RF path / antenna issue).
    uint8_t arc = radio.getARC();
    Serial.printf("  ARC (retries used): %u  (0=CE issue, 15=RF/antenna issue)\n", arc);

    radio.powerDown();
    radio.printDetails();   // dump all NRF24 registers to serial

    if (sent) {
        pass("TX sent + ACK received  (server is listening!)");
    } else if (arc == 0) {
        fail("TX never fired — CE pin (GPIO0) not toggling or radio in bad state");
    } else {
        warn(("TX fired (" + String(arc) + " retries), no ACK — RF path issue (antenna/PA stage)").c_str());
    }
}

// =========================================================================
// TEST: CE pin — toggle GPIO0 slowly so it can be measured with a multimeter
// =========================================================================
void testCEPin() {
    section("CE PIN TEST  (GPIO0 — measure with multimeter)");
    pinMode(NRF_CE, OUTPUT);
    for (uint8_t i = 0; i < 5; i++) {
        digitalWrite(NRF_CE, HIGH);
        Serial.println("  CE = HIGH  (expect ~3.3 V on GPIO0)");
        delay(2000);
        digitalWrite(NRF_CE, LOW);
        Serial.println("  CE = LOW   (expect ~0 V on GPIO0)");
        delay(2000);
    }
    pass("CE toggled 5x — check multimeter matched");
}

// =========================================================================
// TEST: NRF24 RX — listen on all node pipes for 60 s
// =========================================================================
void testNRF24_RX() {
    section("NRF24 RX SCAN  (listening 60 s on pipes 1-5)");

    spiNrf.begin(NRF_SCK, NRF_MISO, NRF_MOSI, NRF_CSN);
    if (!radio.begin(&spiNrf)) {
        fail("begin() failed");
        return;
    }

    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.enableDynamicPayloads();
    radio.enableAckPayload();

    const uint64_t BASE = 0xABCDABCD00LL;
    radio.closeReadingPipe(0);
    for (uint8_t i = 1; i <= 5; i++)
        radio.openReadingPipe(i, BASE + i);

    radio.startListening();
    Serial.println("  Waiting for any packet from nodes 1-5...");

    uint32_t pktCount = 0;
    uint32_t deadline = millis() + 60000UL;

    while (millis() < deadline) {
        uint8_t pipe;
        if (radio.available(&pipe)) {
            uint8_t sz = radio.getDynamicPayloadSize();
            uint8_t buf[32] = {};
            if (sz > 0 && sz <= 32) {
                radio.read(buf, sz);
                pktCount++;
                Serial.printf("  PKT #%lu  pipe=%u  size=%u bytes  hex:", pktCount, pipe, sz);
                for (uint8_t i = 0; i < min(sz, (uint8_t)8); i++)
                    Serial.printf(" %02X", buf[i]);
                if (sz > 8) Serial.print(" ...");
                Serial.println();
            } else {
                radio.flush_rx();
            }
        }
        uint32_t remaining = (deadline - millis()) / 1000;
        static uint32_t lastPrint = 0;
        if (remaining % 10 == 0 && remaining != lastPrint) {
            lastPrint = remaining;
            Serial.printf("  %lu s remaining...\n", remaining);
        }
    }

    radio.stopListening();

    if (pktCount == 0) {
        fail("No packets received — RX path may also be broken, or no nodes transmitting");
    } else {
        pass(("RX OK — " + String(pktCount) + " packet(s) received").c_str());
    }
}

// =========================================================================
// SETUP
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(500);   // give CDC time to enumerate on host

    Serial.println("\n\n");
    Serial.println("=========================================");
    Serial.println(" HW 2.0 DIAGNOSTIC FIRMWARE");
    Serial.printf ("  NODE_ID (hardcoded): %d\n", DEBUG_NODE_ID);
    Serial.println("  Pins: SDA=4 SCL=5 CE=0 CSN=1");
    Serial.println("        SCK=10 MISO=6 MOSI=7");
    Serial.println("=========================================");

    Wire.begin(I2C_SDA, I2C_SCL);
}

// =========================================================================
// LOOP — run all tests every 8 s
// =========================================================================
void loop() {
    cycle++;
    Serial.println();
    Serial.println("=========================================");
    Serial.printf (" DIAGNOSTIC CYCLE %lu\n", cycle);
    Serial.println("=========================================");

    uint8_t i2cFound = testI2C();
    testBME280(i2cFound & 0x01);
    testENS160(i2cFound & 0x02);
    testNRF24();

    Serial.println();
    Serial.println("-----------------------------------------");
    Serial.println(" Next scan in 8 s  (Ctrl+C to stop)");
    Serial.println("-----------------------------------------");

    delay(8000);
}
