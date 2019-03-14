#include <SPI.h>
#include "RF24.h"
#include "printf.h"
#include <avr/sleep.h>
#include <Wire.h> //I2C needed for sensors
#include "HTU21D.h" //Humidity sensor
#include <Adafruit_BMP085.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>

Adafruit_BMP085 bmp;
HTU21D myHumidity; //Create an instance of the humidity sensor

#define NODE_TIMEOUT 30000 // Timeout value for radio messaging
#define LED1 5
#define LED2 6
#define DIP4 A0
#define DIP3 A1
#define DIP2 A2
#define DIP1 A3

unsigned long timer = millis();

bool debug_mode = false;
bool PA_high = false;
bool extra_retry = false;

/* Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 7 & 8 */
RF24 radio(7,8);
/**********************************************************/

byte addresses[][6] = {"1Node","2Node","3node","4node"};
volatile bool watchdogActivated = false;
volatile bool radioReceived = false;
int sleep_iterations = 0;
int receive_init = 0;
int sleepIterations = 0;

bool first_run = true;

struct sensorPayload {
  float sensor1;
  float sensor2;
  float sensor3;
  float sensor4;
};
sensorPayload packet;

ISR(WDT_vect)
{
  // Set the watchdog activated flag.
  // Note that you shouldn't do much work inside an interrupt handler.
  watchdogActivated = true;
}

void setup() {
  Serial.begin(57600);
  
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(DIP1, INPUT);
  pinMode(DIP2, INPUT);
  pinMode(DIP3, INPUT);
  pinMode(DIP4, INPUT);

  bool DIP1_state = digitalRead(DIP1);
  if(DIP1_state){
    Serial.println("DIP 1 ON: Debug state enabled.");
    debug_mode = true;
  }else{
    Serial.println("DIP 1 OFF: Normal state enabled.");
  }

  bool DIP2_state = digitalRead(DIP2);
  if(DIP2_state){
    Serial.println("DIP 2 ON: Extended retry protocol enabled.");
    extra_retry = true;
  }else{
    Serial.println("DIP 2 OFF: Normal retry operation.");
  }
  
  bool DIP3_state = digitalRead(DIP3);
    if(DIP3_state){
    Serial.println("DIP 3 ON");
  }
  
  bool DIP4_state = digitalRead(DIP4);
    if(DIP4_state){
    Serial.println("DIP 4 ON: PA mode HIGH.");
    PA_high = true;
  }else{
    Serial.println("DIP 4 OFF: PA mode LOW.");
  }

  if (!bmp.begin()) {
  Serial.println("Could not find a valid BMP085 sensor, check wiring!");
  while (1) {}
  }

  //Configure the humidity sensor
  myHumidity.begin();
  
  //pinMode(2, INPUT);
  radio.begin();
  printf_begin();
  if(PA_high){
    radio.setPALevel(RF24_PA_HIGH);
  }else{
    radio.setPALevel(RF24_PA_LOW);
  }
  radio.setDataRate(RF24_250KBPS);

  if(extra_retry){
    radio.setRetries(15,15);
  }else{
    radio.setRetries(2,15);
  }
  
  // Open a writing and reading pipe on each radio, with opposite addresses

  radio.openWritingPipe(addresses[1]);
  radio.openReadingPipe(1,addresses[0]);

  // Start the radio listening for data
  radio.startListening();
  delay(1000);
  radio.printDetails();

  noInterrupts();
  
  // Set the watchdog reset bit in the MCU status register to 0.
  MCUSR &= ~(1<<WDRF);
  
  // Set WDCE and WDE bits in the watchdog control register.
  WDTCSR |= (1<<WDCE) | (1<<WDE);

  // Set watchdog clock prescaler bits to a value of 8 seconds.
  WDTCSR = (1<<WDP0) | (1<<WDP3);
  
  // Enable watchdog as interrupt only (no reset).
  WDTCSR |= (1<<WDIE);
  
  // Enable interrupts again.
  interrupts();
  
  Serial.println("READY.");
  //while(1);

  radio.powerDown();
}

void loop() {
  
if (watchdogActivated || first_run)
  {
    first_run = false;
    watchdogActivated = false;
    // Increase the count of sleep iterations and take a sensor
    // reading once the max number of iterations has been hit.
    sleepIterations += 1;
    if (sleepIterations >= sleep_iterations) {

      if(debug_mode){
        digitalWrite(LED1, HIGH);
      }

      bool timeout_fail = false;
      bool TX_fail = false;
      bool ok = false;

      poll_sensors();
      
      // Reset the number of sleep iterations.
      sleepIterations = 0;
    radio.powerUp();
    delay(5);
    radio.startListening();

    unsigned long started_waiting_at = millis();
    bool timeout = false;
    while ( ! radio.available() && ! timeout ) {
        if (millis() - started_waiting_at > NODE_TIMEOUT )
          timeout = true;
    }

        // Describe the results
    if ( timeout )
    {
      //printf("Failed, response timed out.\n");
      Serial.println("Failed, reponse timed out.");
      radio.stopListening();
      timeout_fail = true;
      //radio.begin();
    }
    else
    {
      ok = sendSensordata();
      unsigned long delta = (millis() - started_waiting_at)/1000;
      Serial.print("Waited for: ");
      Serial.print(delta);
      Serial.println(" seconds");
      if(!ok){
        sleep_iterations = 0;
        TX_fail = true;
      }
    }
   
    radio.powerDown();

    if(timeout_fail && debug_mode){
      digitalWrite(LED2, HIGH);
      delay(500);
      digitalWrite(LED2, LOW);
      delay(500);
      digitalWrite(LED2, HIGH);
      delay(500);
      digitalWrite(LED2, LOW);
      delay(500);
      digitalWrite(LED2, HIGH);
      delay(500);
      digitalWrite(LED2, LOW);
    }

    if(TX_fail && debug_mode){
      digitalWrite(LED2, HIGH);
      delay(500);
      digitalWrite(LED2, LOW);
      delay(500);
      digitalWrite(LED2, HIGH);
      delay(500);
      digitalWrite(LED2, LOW);
    }

    if(ok && debug_mode){
      digitalWrite(LED2, HIGH);
      delay(250);
      digitalWrite(LED2, LOW);
    }
    
    if(debug_mode){
        digitalWrite(LED1, LOW);
      }
    }
}

sleep();


} // Loop

void sleep()
{
  // Set sleep to full power down.  Only external interrupts or 
  // the watchdog timer can wake the CPU!
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  wdt_reset();

  // Turn off the ADC while asleep.
  power_adc_disable();

  // Enable sleep and enter sleep mode.
  sleep_mode();

  // CPU is now asleep and program execution completely halts!
  // Once awake, execution will resume at this point.
  
  // When awake, disable sleep mode and turn on all devices.
  sleep_disable();
  power_all_enable();
}

bool sendSensordata(){

      //if( radio.available()){
                                                                    // Variable for the received timestamp
      //while (radio.available()) {                                   // While there is data ready
      float rec_buffer;
        radio.read( &rec_buffer, sizeof(float) );             // Get the payload
      //}

      sleep_iterations = rec_buffer;
      Serial.print("Received sleep_iterations: ");
      Serial.println(rec_buffer);
      radio.stopListening();                                        // First, stop listening so we can talk   
      bool ok = radio.write( &packet, sizeof(sensorPayload) );              // Send the final one back.      
      radio.startListening();                                       // Now, resume listening so we catch the next packets.     
      Serial.println(F("Sent response"));
      if(ok) {
        Serial.println("TX OK.");
        return true;
      }else{
        Serial.println("TX FAILED.");
        return false;
      }
      

   //}


}

void poll_sensors(){
  
  packet = {myHumidity.readTemperature(), myHumidity.readHumidity(), (bmp.readPressure())/100, 44.44};
  Serial.print("Temperature = ");
  Serial.println(packet.sensor1);
  Serial.print("Humidity = ");
  Serial.println(packet.sensor2);
  Serial.print("Pressure = ");
  Serial.println(packet.sensor3);
 }

