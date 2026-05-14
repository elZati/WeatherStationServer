#include <SPI.h>
#include "RF24.h"
#include <Wire.h>
#include "SparkFunHTU21D.h" 
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>

// ==========================================
// UNIQUE SETTING: Change this for each node!
// Node 1 = Outdoor, Node 2 = Indoor, etc.
// ==========================================
const int32_t NODE_ID = 2; 

// --- Hardware Setup ---
RF24 radio(7, 8); 
HTU21D myHumidity;
volatile bool watchdogActivated = false;
bool first_run = true;

// --- Addressing Logic (Using working 64-bit logic) ---
const uint64_t baseAddress = 0xABCDABCD00LL;
const uint64_t myAddress   = baseAddress + NODE_ID;

// Struct must match the Raspberry Pi exactly
struct __attribute__((packed)) sensorPayload {
  int32_t nodeID;
  float sensor1; // Temperature
  float sensor2; // Humidity
  float sensor3; // Pressure placeholder
  float sensor4; // Battery Voltage
};
sensorPayload packet;

int sleep_iterations = 1; 
int current_sleep_count = 0;

ISR(WDT_vect) { 
  watchdogActivated = true;
}

void setup() {
  Serial.begin(57600);
  while (!Serial); 
  Serial.println(F("--- GEMINI DEBUG START ---"));
  
  packet.nodeID = NODE_ID;

  Serial.print(F("Node ID: ")); Serial.println(NODE_ID);
  Serial.print(F("Target Address: 0xABCDABCD0")); Serial.println(NODE_ID);

  myHumidity.begin();

  if (!radio.begin()) {
    Serial.println(F("CRITICAL: Radio hardware NOT found!"));
    while (1);
  }

  // Protocol Settings
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setCRCLength(RF24_CRC_16);
  radio.setAutoAck(true);
  radio.enableDynamicPayloads();
  radio.enableAckPayload();

  // Open writing pipe with 64-bit address [cite: 174]
  radio.openWritingPipe(myAddress);
  
  Serial.print(F("Radio Connected: "));
  Serial.println(radio.isChipConnected() ? F("YES") : F("NO"));
  
  radio.powerDown();
  setup_watchdog();
  
  Serial.println(F("System Ready. Waiting for Watchdog..."));
}

void loop() {
  if (watchdogActivated || first_run) {
    first_run = false;
    watchdogActivated = false;
    current_sleep_count++;

    if (current_sleep_count >= sleep_iterations) {
      current_sleep_count = 0;
      
      Serial.println(F("\n--- Wake Cycle ---"));

      // 1. POLL SENSORS (Includes "998" error fixes)
      poll_sensors();

      // 2. TRANSMIT DATA
      radio.powerUp();
      delay(10);
      
      Serial.print(F("TX Status: "));
      bool ok = radio.write(&packet, sizeof(packet));
      
      if (ok) {
        Serial.println(F("SUCCESS (ACK Received)"));
        if (radio.isAckPayloadAvailable()) {
          float server_cmd;
          radio.read(&server_cmd, sizeof(float));
          if (server_cmd > 0) {
            sleep_iterations = (int)server_cmd;
            Serial.print(F("New Sleep Setting: ")); Serial.println(sleep_iterations);
          }
        }
      } else {
        Serial.println(F("FAILED (No response from Pi)"));
      }
      
      radio.powerDown();
      Serial.println(F("--- Sleeping ---"));
      Serial.flush();
    }
  }
  enter_deep_sleep();
}

// --- Subroutines with Error Handling ---

void poll_sensors() {
  // Fix for "998" errors: Hard reset the I2C bus [cite: 113]
  Serial.println(F("I2C: Resetting Bus..."));
  Wire.end(); 
  delay(20);
  Wire.begin();
  
  myHumidity.begin();
  delay(100); 
  
  // Read Temperature with validation [cite: 151]
  float t = myHumidity.readTemperature();
  
  // If reading is out of realistic range (the "998" error), retry [cite: 152]
  if (t > 150.0 || t < -50.0) {
      Serial.println(F("Sensor: Data Error! Retrying..."));
      delay(150);
      t = myHumidity.readTemperature();
  }
  
  packet.sensor1 = t;

  float h = myHumidity.readHumidity();
  if (h > 100.0 || h < 0.0) {
      delay(150);
      h = myHumidity.readHumidity();
  }
  packet.sensor2 = (h > 100.0 || h < 0.0) ? -99.9 : h;

  packet.sensor3 = 33.33; // Placeholder
  packet.sensor4 = getBatteryVoltage(); 

  Serial.print(F("Data: T=")); Serial.print(packet.sensor1);
  Serial.print(F(" H=")); Serial.print(packet.sensor2);
  Serial.print(F(" V=")); Serial.println(packet.sensor4);
}

void setup_watchdog() {
  noInterrupts();
  MCUSR &= ~(1 << WDRF);
  WDTCSR |= (1 << WDCE) | (1 << WDE);
  WDTCSR = (1 << WDP0) | (1 << WDP3); // 8 second interval [cite: 187]
  WDTCSR |= (1 << WDIE);
  interrupts();
}

void enter_deep_sleep() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  byte old_ADCSRA = ADCSRA;
  ADCSRA = 0; // Turn off ADC [cite: 188]
  sleep_cpu();
  sleep_disable();
  ADCSRA = old_ADCSRA;
}

float getBatteryVoltage() {
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2);
  ADCSRA |= _BV(ADSC);
  while (bit_is_set(ADCSRA, ADSC));
  uint8_t low = ADCL;
  uint8_t high = ADCH;
  long result = (high << 8) | low;
  result = 1125300L / result; // Vcc in mV [cite: 163, 190]
  return (float)result / 1000.0;
}
