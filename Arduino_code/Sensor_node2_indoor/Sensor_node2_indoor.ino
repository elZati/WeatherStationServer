
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



HTU21D myHumidity; //Create an instance of the humidity sensor



unsigned long timer = millis();


/****************** User Config ***************************/
/***      Set this radio as radio number 0 or 1         ***/
bool radioNumber = 1;

/* Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 7 & 8 */
RF24 radio(7,8);
/**********************************************************/

byte addresses[][6] = {"1Node","2Node","3node"};

// Used to control whether this node is sending or receiving
bool role = 0;
volatile bool radioReceived = false;
int initialize_code = 12345;
int receive_init = 0;
float sensor_val = 23.5;

struct sensorPayload {
  float sensor1;
  float sensor2;
  float sensor3;
  float sensor4;
};

sensorPayload packet = {11.11, 22.22, 33.33, 44.44};

void setup() {
  Serial.begin(57600);


  //Configure the humidity sensor
  //myHumidity.begin();
  
  pinMode(2, INPUT);
  radio.begin();
  printf_begin();
  // Set the PA Level low to prevent power supply related issues since this is a
 // getting_started sketch, and the likelihood of close proximity of the devices. RF24_PA_MAX is default.
  radio.setPALevel(RF24_PA_MAX);
  radio.setRetries(15,15);
  
  // Open a writing and reading pipe on each radio, with opposite addresses

    radio.openWritingPipe(addresses[1]);
    radio.openReadingPipe(1,addresses[0]);
  radio.setChannel(99);
  // Start the radio listening for data
  radio.startListening();
  delay(1000);
  radio.printDetails();
  interrupts();
}

void loop() {
  
/****************** Sensor Node Role ***************************/
//sleepNow();

if(radio.available())
{
 poll_sensors();
 bool ok = sendSensordata();
}


} // Loop

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

      if( radio.available()){
                                                                    // Variable for the received timestamp
      while (radio.available()) {                                   // While there is data ready
        radio.read( &receive_init, sizeof(int) );             // Get the payload
      }

      if(receive_init == initialize_code)
     {
      radio.stopListening();                                        // First, stop listening so we can talk   
      radio.write( &packet, sizeof(sensorPayload) );              // Send the final one back.      
      radio.startListening();                                       // Now, resume listening so we catch the next packets.     
      Serial.println(F("Sent response"));
      return true;
     } else {
      Serial.println("Wrong init");
      return false;
     }
   }


}

void poll_sensors(){

  //packet.sensor2 = myHumidity.readHumidity();
  //packet.sensor1 = myHumidity.readTemperature();

  //packet.sensor2 = myHumidity.readHumidity();
  //packet.sensor1 = myHumidity.readTemperature();

  Serial.print("Temperature = ");
  Serial.println(packet.sensor1);
  Serial.print("Humidity = ");
  Serial.println(packet.sensor2);

}

