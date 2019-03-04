
/*
* Getting Started example sketch for nRF24L01+ radios
* This is a very basic example of how to send data from one node to another
* Updated: Dec 2014 by TMRh20
*/

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

#define LOGGING_FREQ_SECONDS 16 // Seconds to wait before a new sensor reading is logged.
#define MAX_SLEEP_ITERATIONS   LOGGING_FREQ_SECONDS / 8  // Number of times to sleep (for 8 seconds) before
                                                         // a sensor reading is taken and sent to the server.
                                                         // Don't change this unless you also change the 
#define NODE_TIMEOUT 16000 // Timeout value for radio messaging


unsigned long timer = millis();


/****************** User Config ***************************/
/***      Set this radio as radio number 0 or 1         ***/
bool radioNumber = 1;

/* Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 7 & 8 */
RF24 radio(7,8);
/**********************************************************/

byte addresses[][6] = {"1Node","2Node","3node","4node"};
volatile bool watchdogActivated = false;
// Used to control whether this node is sending or receiving
bool role = 0;
volatile bool radioReceived = false;
int initialize_code = 12345;
int sleep_iterations = 0;
int receive_init = 0;
float sensor_val = 23.5;
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
  randomSeed(analogRead(0));
  packet = {random(10, 20), 22.22, 33.33, 44.44};

  if (!bmp.begin()) {
  Serial.println("Could not find a valid BMP085 sensor, check wiring!");
  while (1) {}
  }

  //Configure the humidity sensor
  myHumidity.begin();
  
  //pinMode(2, INPUT);
  radio.begin();
  printf_begin();
  // Set the PA Level low to prevent power supply related issues since this is a
 // getting_started sketch, and the likelihood of close proximity of the devices. RF24_PA_MAX is default.
  radio.setPALevel(RF24_PA_HIGH);
  //radio.setAutoAck(1);
  //radio.enableDynamicPayloads();
  radio.setDataRate(RF24_1MBPS);
  radio.setRetries(2,15);

  
  // Open a writing and reading pipe on each radio, with opposite addresses

    radio.openWritingPipe(addresses[0]);
    radio.openReadingPipe(1,addresses[2]);

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
  
/****************** Sensor Node Role ***************************/
//sleepNow();

 
//if(radio.available())
//{
// poll_sensors();
// bool ok = sendSensordata();
//}

if (watchdogActivated || first_run)
  {
    first_run = false;
    watchdogActivated = false;
    // Increase the count of sleep iterations and take a sensor
    // reading once the max number of iterations has been hit.
    sleepIterations += 1;
    if (sleepIterations >= sleep_iterations) {
      // Reset the number of sleep iterations.
      sleepIterations = 0;
      // Log the sensor data (waking the CC3000, etc. as needed)
    //delay(random(1000,2000));
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
      //radio.begin();
    }
    else
    {
      poll_sensors();
      bool ok = sendSensordata();
      unsigned long delta = (millis() - started_waiting_at)/1000;
      Serial.print("Waited for: ");
      Serial.print(delta);
      Serial.println(" seconds");
      if(!ok){
        sleep_iterations = 0;
      }
    }
   
    radio.powerDown();
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

void wake ()
{
  sleep_disable ();         // first thing after waking from sleep:
  detachInterrupt (digitalPinToInterrupt (2));      // stop LOW interrupt on D2
  radioReceived = true;
}  // end of wake

void sleepNow ()
{
  //Serial.println("Going to Sleep..");
  set_sleep_mode (SLEEP_MODE_PWR_DOWN);   
  noInterrupts ();          // make sure we don't get interrupted before we sleep
  sleep_enable ();          // enables the sleep bit in the mcucr register
  attachInterrupt (digitalPinToInterrupt (2), wake, FALLING);  // wake up on low level on D2
  interrupts ();           // interrupts allowed now, next instruction WILL be executed
  sleep_cpu ();            // here the device is put to sleep
}  // end of sleepNow

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

  //packet.sensor2 = myHumidity.readHumidity();
  //packet.sensor1 = myHumidity.readTemperature();

  //packet.sensor2 = myHumidity.readHumidity();
  //packet.sensor1 = myHumidity.readTemperature();
  packet = {myHumidity.readTemperature(), myHumidity.readHumidity(), (bmp.readPressure())/100, 44.44};
  Serial.print("Temperature = ");
  Serial.println(packet.sensor1);
  Serial.print("Humidity = ");
  Serial.println(packet.sensor2);
  Serial.print("Pressure = ");
  Serial.println(packet.sensor3);
 }

