#include <SPI.h>
#include "RF24.h"
#include "printf.h"
#include <Wire.h> //I2C needed for sensors
#include "HTU21D.h" //Humidity sensor
#include <Adafruit_BMP085.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>

Adafruit_BMP085 bmp;
HTU21D myHumidity; //Create an instance of the humidity sensor

#define LOGGING_FREQ_SECONDS 24 // Seconds to wait before a new sensor reading is logged.
#define MAX_SLEEP_ITERATIONS   LOGGING_FREQ_SECONDS / 8  // Number of times to sleep (for 8 seconds) before
                                                         // a sensor reading is taken and sent to the server.
                                                         // Don't change this unless you also change the 

unsigned long timer = millis();

/****************** User Config ***************************/
/***      Set this radio as radio number 0 or 1         ***/
bool radioNumber = 1;

/* Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 7 & 8 */
RF24 radio(7,8);
/**********************************************************/

byte addresses[][6] = {"1Node","2Node"};

// Used to control whether this node is sending or receiving
bool role = 0;
volatile bool radioReceived = false;

volatile bool watchdogActivated = false;
int sleepIterations = 0;

int initialize_code = 12345;
int receive_init = 0;
float sensor_val = 23.5;

struct sensorPayload {
  float sensorID;
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
  //Configure the pressure sensor
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
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
   radio.setRetries(15,15);
  // Open a writing and reading pipe on each radio, with opposite addresses

    radio.openWritingPipe(addresses[0]);
    radio.openReadingPipe(1,addresses[1]);
  
  // Start the radio listening for data
  radio.startListening();
  delay(1000);
  radio.printDetails();
  delay(1000);
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
if (watchdogActivated)
  {
    watchdogActivated = false;
    // Increase the count of sleep iterations and take a sensor
    // reading once the max number of iterations has been hit.
    sleepIterations += 1;
    if (sleepIterations >= MAX_SLEEP_ITERATIONS) {
      // Reset the number of sleep iterations.
      sleepIterations = 0;
      // Log the sensor data (waking the CC3000, etc. as needed)
    delay(random(1000,2000));
    radio.powerUp();
    delay(5);
    sendSensordata2(random(1000,2000),3);
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

bool sendSensordata2(int delayTime, int tryCount){

  poll_sensors();
  bool ok = false;
  bool max_attempts = false;
  int att_counter = 0;

  while(!ok && !max_attempts){
    radio.stopListening();                                        // First, stop listening so we can talk   
    ok=radio.write( &packet, sizeof(sensorPayload) );              // Send the final one back.
    radio.startListening();      
    if(ok){
      Serial.println("Write OK.");
      return true;
    }else{
      Serial.println("Write FAILED.");
     }
     
    delay(delayTime+random(100, 300));
    att_counter++;
    if(att_counter >= tryCount){
      max_attempts = true;
      Serial.print("Maximum attempts to TX exceeded!");
      return false;
    }
  }
}

bool sendSensordata(){

      //if( radio.available()){
                                                                    // Variable for the received timestamp
      //while (radio.available()) {                                   // While there is data ready
        radio.read( &receive_init, sizeof(int) );             // Get the payload
      //}

      if(receive_init == initialize_code)
     {
      radio.stopListening();                                        // First, stop listening so we can talk   
      radio.write( &packet, sizeof(sensorPayload) );              // Send the final one back.      
      radio.startListening();                                       // Now, resume listening so we catch the next packets.     
      Serial.print(F("Sent response"));
      return true;
     } else {
      Serial.println("Wrong init");
      return false;
     }
   //}
}

void poll_sensors(){

  packet.sensor2 = myHumidity.readHumidity();
  packet.sensor1 = myHumidity.readTemperature();
  packet.sensor3 = (bmp.readPressure())/100;

  Serial.print("Temperature = ");
  Serial.println(packet.sensor1);
  Serial.print("Pressure = ");
  Serial.println(packet.sensor3);
  Serial.print("Humidity = ");
  Serial.println(packet.sensor2);

  packet.sensorID = 0;
  Serial.print("SensorID = ");
  Serial.println(packet.sensorID);

}

