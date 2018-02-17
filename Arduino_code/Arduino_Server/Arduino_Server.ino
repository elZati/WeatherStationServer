
/*
* Getting Started example sketch for nRF24L01+ radios
* This is a very basic example of how to send data from one node to another
* Updated: Dec 2014 by TMRh20
*/

#include <SPI.h>
#include "RF24.h"
#include <avr/sleep.h>
#include <Wire.h> //I2C needed for sensors
#include "HTU21D.h" //Humidity sensor

/*
 Weather Station Server for RPi

 */


#define NODE_RETRY 3 // Number of radio retries per node
#define NODE_TIMEOUT 300 // Timeout value for radio messaging
#define NODE_PRINTOUT_DELAY 2000 //Delay between printing node values 


using namespace std;


RF24 radio(9,10); //CE, CSN

struct SensorPayload {
  float sensor1;
  float sensor2;
  float sensor3;
  float sensor4;
};

SensorPayload SensorNode1;
SensorPayload SensorNode2;


// function declaration

/********** User Config *********/
// Assign a unique identifier for this node, 0 or 1
bool radioNumber = 0;
const int initialize_cmd = 12345;
float sensor1 = 0;
/********************************/

//********* Sensor Config ***********
int number_of_nodes = 2; //enter amount of active nodes 

bool node1_enable = true;
bool node2_enable = true;
bool node3_enable = false;
bool node4_enable = false;
bool node5_enable = false;

String  node1_name = "OUTDOOR SENSOR"; //enter name of node
String node2_name = "INDOOR SENSOR";



//***********************************

// Radio pipe addresses for the 2 nodes to communicate.
const uint8_t pipes[][6] = {"1Node","2Node","3node"};

long last_printout = millis();


void setup() {
  Serial.begin(57600);
  // Setup and configure rf radio
  radio.begin();

  // optionally, increase the delay between retries & # of retries
  radio.setRetries(15,15);
  radio.setPALevel(RF24_PA_HIGH);
  //radio.setAutoAck(1);
  //radio.enableDynamicPayloads();
  radio.setDataRate(RF24_1MBPS);



  radio.openReadingPipe(1,pipes[1]);
    radio.printDetails();
}

void loop() {

  
  // forever loop

      retryFetchSensor(0, 100, 50);
      retryFetchSensor(2, 100, 50);

      delay(2000);
      printNodes();
}

void retryFetchSensor(int nodeAddress, int max_attemptCount, int delayTime) {

  bool max_attempts = false;
  int att_counter = 0;
  bool ok = false;

  while(!ok && !max_attempts){
    delay(delayTime);
    ok = fetchSensor(nodeAddress);
    att_counter++;
    if(att_counter > max_attemptCount){
      max_attempts = true;
      Serial.print("Max attempts exceeded for sensor ");
      Serial.println(nodeAddress);
    }
  }

//    Serial.print("It took ");  
//    Serial.print(att_counter); 
//    Serial.print(" attempts for sensor "); 
//    Serial.println(nodeAddress);

  
}

bool fetchSensor(int nodeAddress) {
  
  
  SensorPayload buffer;
  radio.openWritingPipe(pipes[nodeAddress]);
  radio.openReadingPipe(1,pipes[1]);
  radio.stopListening();
    
    // Take the time, and send it.  This will block until complete

    //Serial.println("Now sending...");


    bool ok = radio.write( &initialize_cmd, sizeof(int) );

    if (!ok){
        //Serial.println("Failed to write.\n");
        //radio.begin();
        return false;
    }
    // Now, continue listening
    //Serial.println(" ..OK.");
    radio.startListening();

    // Wait here until we get a response, or timeout (250ms)
    unsigned long started_waiting_at = millis();
    bool timeout = false;
    while ( ! radio.available() && ! timeout ) {
        if (millis() - started_waiting_at > NODE_TIMEOUT )
          timeout = true;
    }


    // Describe the results
    if ( timeout )
    {
      //Serial.println("Failed, response timed out.");
      radio.stopListening();
      //radio.begin();
      return false;
    }
    else
    {

      // Grab the response, compare, and send to debugging spew
        
      radio.read( &buffer, sizeof(SensorPayload) );
      
      if(nodeAddress == 0)
      {
        SensorNode1 = buffer;
      }
      if(nodeAddress == 2)
      {
        SensorNode2 = buffer;
      }
      radio.stopListening();
      //radio.begin();
      return true;
    }
  
}

void printNodes(){
  
if (millis()-last_printout > NODE_PRINTOUT_DELAY)
{
  
  
  if(node1_enable) {
      Serial.println("OUTDOOR SENSOR1");

      // Spew it
Serial.print("Temperature 0: ");
Serial.println(SensorNode1.sensor1);
//      Serial.println("Humidity: %4.1f %%RH\n",SensorNode1.sensor2);
//      Serial.println("Air Pressure: %5.1f hPa\n",SensorNode1.sensor3);
//      Serial.println("********************************************************* \n");
//      Serial.println("\n");
  }
  
  if(node2_enable) {
      Serial.println("INDOOR SENSOR1");
Serial.print("Temperature 2: ");
Serial.println(SensorNode2.sensor1);

      // Spew it
//      Serial.println("Temperature: %4.1f \302\260C\n",SensorNode2.sensor1);
//      Serial.println("Humidity: %4.1f %%RH\n",SensorNode2.sensor2);
//      Serial.println("********************************************************* \n");
//      Serial.println("\n");
  }
  last_printout = millis();
}
  
Serial.println(" ");
  return;
}


