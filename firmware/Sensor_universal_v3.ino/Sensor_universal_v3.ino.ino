/*
 * =========================================================================
 * GEMINI UNIVERSAL FIRMWARE (v14.9.5 - THE MASTER DIAGNOSTIC)
 * -------------------------------------------------------------------------
 * This is the most advanced debugging version designed to solve Node 3's 
 * specific issues (Negative Humidity, Stuck Sleep Timer, and ACK Drops).
 * * * ARCHITECTURAL FEATURES:
 * 1.  I2C BUS SCANNER: Runs at boot to verify physical sensor connection.
 * 2.  RAW SENSOR PREVIEW: Prints data BEFORE transmission to isolate
 * sensor hardware issues from radio/Pi alignment issues.
 * 3.  MANUAL OVERRIDE (DIP 1): Ground DIP 1 to ignore Pi sleep commands 
 * and use a local default (useful if Pi ACK is empty).
 * 4.  BYTE-PACKED STRUCT: Matches the Pi's 20-byte memory map perfectly.
 * 5.  EXTENSIVE COMMENTING: For long-term maintainability.
 * =========================================================================
 */

#include <SPI.h>            // SPI for nRF24L01 Radio
#include "RF24.h"           // nRF24 Radio Library
#include <Wire.h>           // I2C for HTU21D and BMP180
#include "SparkFunHTU21D.h" // Humidity sensor lib
#include <Adafruit_BMP085.h>// Pressure sensor lib
#include <avr/sleep.h>      // ATMega power management
#include <avr/power.h>      // ATMega power management
#include <avr/wdt.h>        // Watchdog Timer for 8s sleep cycles

// =========================================================================
// UNIQUE SETTING: Node ID (Set to 3 for Garden/Pressure Node)
// =========================================================================
const int32_t NODE_ID = 3; 

// Hardware Pin Definitions
#define LED1 5   // Activity LED (Blue) - Stays ON while node is awake
#define LED2 6   // Status LED (Green) - Blinks on successful transmission
#define DIP1 A3  // MANUAL OVERRIDE: Ground this to ignore Pi sleep commands
#define DIP2 A2  // RANGE EXTENDER: Ground this for maximum radio retries (15,15)
#define DIP3 A1  // BATT SAVER: Ground this to double the current sleep interval
#define DIP4 A0  // PA LEVEL: Ground this for High Power (PA_HIGH) radio mode

// --- Hardware Objects ---
RF24 radio(7, 8); 
HTU21D myHumidity;
Adafruit_BMP085 bmp;

// --- Global State ---
bool hasBMP = false; 
bool hasHTU = false;
volatile bool watchdogActivated = false;
bool first_run = true;

// 64-bit Address Logic matching the Pi Server v13.x
const uint64_t baseAddress = 0xABCDABCD00LL;
const uint64_t myAddress   = baseAddress + NODE_ID;

/*
 * THE 20-BYTE PAYLOAD
 * ------------------
 * We use __attribute__((packed)) to ensure the Arduino doesn't add
 * hidden "padding" bytes. This must match the Raspberry Pi's struct.
 */
struct __attribute__((packed)) sensorPayload {
  int32_t nodeID;  // Unique ID (4 bytes)
  float sensor1;   // Temperature (4 bytes)
  float sensor2;   // Humidity (4 bytes)
  float sensor3;   // Pressure (4 bytes)
  float sensor4;   // Battery Volts (4 bytes)
};
sensorPayload packet;

// --- Sleep & Timing Variables ---
int sleep_multiplier = 1;      // This is updated by the Pi's ACK payload
int manual_sleep_value = 5;    // The default multiplier if DIP 1 is active (40s)
int current_sleep_count = 0;   // Progress toward next transmission

// Interrupt service routine for the Watchdog Timer
ISR(WDT_vect) { watchdogActivated = true; }

void setup() {
  /*
   * 1. SYSTEM INITIALIZATION
   * Serial is set to 57600 for the 8MHz Pro Mini clock timing.
   */
  Serial.begin(57600);
  delay(1000); 
  
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(DIP1, INPUT_PULLUP);
  pinMode(DIP2, INPUT_PULLUP);
  pinMode(DIP3, INPUT_PULLUP);
  pinMode(DIP4, INPUT_PULLUP);

  Serial.println(F("\n\n####################################"));
  Serial.print(F("  NODE ")); Serial.print(NODE_ID); Serial.println(F(" | v14.9.5 MASTER DIAGNOSTIC"));
  Serial.println(F("####################################"));

  /*
   * 2. I2C SCANNER (The Hardware Detective)
   * This verifies if the sensors are physically connected.
   * HTU21D is usually 0x40. BMP180 is usually 0x77.
   */
  Wire.begin();
  Serial.println(F("Scanning I2C Bus..."));
  byte error, address;
  int nDevices = 0;
  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print(F("Found device at 0x"));
      if (address<16) Serial.print("0");
      Serial.print(address,HEX);
      if (address == 0x40) { Serial.println(F(" [HTU21D Detected]")); hasHTU = true; }
      else if (address == 0x77) { Serial.println(F(" [BMP180 Detected]")); hasBMP = true; }
      else { Serial.println(F(" [Unknown Device]")); }
      nDevices++;
    }
  }
  if (nDevices == 0) Serial.println(F("CRITICAL: No I2C hardware found!"));

  // Initialize sensors found during the scan
  if (hasBMP) bmp.begin();
  if (hasHTU) myHumidity.begin();

  /*
   * 3. RADIO INITIALIZATION
   * Matches the exact working configuration from Node 3 reference.
   */
  if (!radio.begin()) {
    Serial.println(F("CRITICAL: Radio Hardware Failure!"));
    while (1); // Halt if radio is missing
  }
  
  radio.setDataRate(RF24_250KBPS); // Long range, low speed
  radio.setCRCLength(RF24_CRC_16);
  radio.setAutoAck(true);          // Required for ACK Payloads
  radio.enableDynamicPayloads();   
  radio.enableAckPayload();        // Allows Pi to send sleep commands back
  
  // PA Power set by DIP 4
  radio.setPALevel(!digitalRead(DIP4) ? RF24_PA_HIGH : RF24_PA_LOW);
  
  // Retry persistence set by DIP 2
  if(!digitalRead(DIP2)) {
    radio.setRetries(15, 15);
    Serial.println(F("Radio: High-Retry Mode Enabled."));
  } else {
    radio.setRetries(2, 15);
  }

  radio.openWritingPipe(myAddress);
  packet.nodeID = NODE_ID;

  Serial.println(F("System Status: READY."));
  Serial.println(F("####################################\n"));
  Serial.flush();

  radio.powerDown();
  setup_watchdog();
}

void loop() {
  if (watchdogActivated || first_run) {
    first_run = false;
    watchdogActivated = false;
    current_sleep_count++;

    // Determine the Sleep Target
    // If DIP 1 is ON (grounded), we use the manual setting (5).
    int base_val = (!digitalRead(DIP1)) ? manual_sleep_value : sleep_multiplier;
    
    // If DIP 3 is ON, we double the value.
    int effective_multiplier = !digitalRead(DIP3) ? (base_val * 2) : base_val;

    if (current_sleep_count >= effective_multiplier) {
      current_sleep_count = 0;
      digitalWrite(LED1, HIGH); // Blue Wake LED ON
      
      poll_sensors();

      // --- DIAGNOSTIC REPORT ---
      Serial.println(F("===================================="));
      Serial.print(F("NODE ")); Serial.print(NODE_ID); Serial.println(F(" TRANSMISSION PREVIEW"));
      Serial.println(F("------------------------------------"));
      
      // 1. RAW DATA CHECK: Is the sensor broken or is it a Pi error?
      Serial.println(F("[ RAW SENSOR PREVIEW ]"));
      Serial.print(F(" Temperature: ")); Serial.print(packet.sensor1); Serial.println(F(" C"));
      Serial.print(F(" Humidity:    ")); Serial.print(packet.sensor2); Serial.println(F(" %"));
      Serial.print(F(" Pressure:    ")); Serial.print(packet.sensor3); Serial.println(F(" hPa"));
      Serial.print(F(" Battery:     ")); Serial.print(packet.sensor4); Serial.println(F(" V"));
      
      // 2. TIMING STATUS: Why is it sleeping for this long?
      Serial.println(F("\n[ TIMING STATUS ]"));
      Serial.print(F(" Control:     ")); Serial.println(!digitalRead(DIP1) ? F("MANUAL (DIP 1 ON)") : F("PI CONTROLLED"));
      Serial.print(F(" Total Sleep: ")); Serial.print(effective_multiplier * 8); Serial.println(F(" seconds"));
      Serial.println(F("------------------------------------"));

      // 3. RADIO ACTION
      radio.powerUp();
      delay(50); 
      Serial.print(F("Radio: Sending 20-byte packet... "));
      bool ok = radio.write(&packet, sizeof(packet));

      if (ok) {
        Serial.println(F("SUCCESS (ACK Received)"));
        blinkLED(LED2, 1); // Single Green blink

        // Read updated sleep command from the Pi
        if (radio.isAckPayloadAvailable()) {
          float server_cmd;
          radio.read(&server_cmd, sizeof(float));
          if (server_cmd > 0) {
            Serial.print(F(">>> PI UPDATE: New Sleep Multiplier = ")); Serial.println((int)server_cmd);
            sleep_multiplier = (int)server_cmd;
          }
        } else {
          Serial.println(F(">>> PI STATUS: ACK Payload is EMPTY."));
        }
      } else {
        Serial.println(F("FAILED (No response)"));
        blinkLED(LED2, 3); // Triple Green blink
      }
      
      Serial.println(F("====================================\n"));
      radio.powerDown();
      digitalWrite(LED1, LOW); // Blue Wake LED OFF
      Serial.flush();
    }
  }
  enter_deep_sleep();
}

void poll_sensors() {
  /*
   * I2C RECOVERY: Restarts the bus to prevent "lockups"
   */
  Wire.end();
  delay(20);
  Wire.begin();
  
  if (hasHTU) {
    myHumidity.begin();
    packet.sensor1 = myHumidity.readTemperature();
    packet.sensor2 = myHumidity.readHumidity();
    if (packet.sensor2 < 0 || packet.sensor2 > 100) {
      delay(150);
      packet.sensor2 = myHumidity.readHumidity();
    }
    if (packet.sensor2 < 0 || packet.sensor2 > 100) {
      packet.sensor2 = -99.9;
    }
  } else {
    packet.sensor1 = 0.0;
    packet.sensor2 = 0.0;
  }
  
  if (hasBMP) {
    bmp.begin();
    packet.sensor3 = bmp.readPressure() / 100.0F; 
  } else {
    packet.sensor3 = 0.00;
  }
  
  packet.sensor4 = getBatteryVoltage();
}

void blinkLED(int pin, int times) {
  for(int i=0; i<times; i++) {
    digitalWrite(pin, HIGH); delay(100);
    digitalWrite(pin, LOW); delay(100);
  }
}

void setup_watchdog() {
  noInterrupts();
  MCUSR &= ~(1 << WDRF);
  WDTCSR |= (1 << WDCE) | (1 << WDE);
  WDTCSR = (1 << WDP0) | (1 << WDP3); // Binary for 8.0s
  WDTCSR |= (1 << WDIE);
  interrupts();
}

void enter_deep_sleep() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  byte old_ADCSRA = ADCSRA; 
  ADCSRA = 0; // Turn off ADC to save battery
  sleep_cpu();
  sleep_disable();
  ADCSRA = old_ADCSRA;
}

float getBatteryVoltage() {
  /*
   * Measures the 3.3V rail against the internal 1.1V reference.
   */
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2);
  ADCSRA |= _BV(ADSC);
  while (bit_is_set(ADCSRA, ADSC));
  uint8_t low = ADCL, high = ADCH;
  long result = (high << 8) | low;
  result = 1125300L / result;
  return (float)result / 1000.0;
}
