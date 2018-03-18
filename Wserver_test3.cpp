/*
 Weather Station Server for RPi

 */
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include "RF24.h"
#include <ctime>

#define NODE_RETRY 3 // Number of radio retries per node
#define NODE_TIMEOUT 300 // Timeout value for radio messaging
#define NODE_PRINTOUT_DELAY 2000 //Delay between printing node values 


using namespace std;


RF24 radio(22,0);

struct SensorPayload {
	float sensor1;
	float sensor2;
	float sensor3;
	float sensor4;
};

SensorPayload SensorNode1;
SensorPayload SensorNode2;


// function declaration
bool fetchSensor(int nodeAddress);
void printNodes();
void retryFetchSensor(int nodeAddress, int max_attemptCount, int delayTime);
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

string  node1_name = "OUTDOOR SENSOR"; //enter name of node
string node2_name = "INDOOR SENSOR";
 

//***********************************

// Radio pipe addresses for the 2 nodes to communicate.
const uint8_t pipes[][6] = {"1Node","2Node","3node"};

long last_printout = millis();


int main(int argc, char** argv){

	cout << "Weather Station Server for RPi" << endl;
	time_t now = time(0);
	tm *ltm = localtime(&now);


	cout << "Time: " << ltm->tm_hour << ":";
	if(ltm->tm_min >= 10){
	cout << ltm->tm_min << ":";
}
else{
cout << "0" << ltm->tm_min << ":";
}
	cout << ltm->tm_sec << endl;

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
	sleep(1);
	
	// forever loop
	while (1)
	{
	
/* 		retryFetchSensor(0, 5, 100);
		retryFetchSensor(2, 5, 100);
		sleep(2);
		printNodes(); */
		
				bool ok = fetchSensor(0);
			if(!ok) {
				printf("*** Node address 0 failed. ** \n");
			}
			bool ok2 = fetchSensor(2);
			if(!ok2) {
				printf("*** Node address 2 failed. ** \n");
			}
			sleep(2);
			printNodes();	
	
	

	} // forever loop
	


  return 0;
}
void retryFetchSensor(int nodeAddress, int max_attemptCount, int delayTime) {

  bool max_attempts = false;
  int att_counter = 0;
  bool ok = false;

  while(!ok && !max_attempts){
    sleep(delayTime);
    ok = fetchSensor(nodeAddress);
    att_counter++;
    if(att_counter > max_attemptCount){
      max_attempts = true;
      printf("Max attempts exceeded for sensor %1d \n",nodeAddress);
    }
  }
}

bool fetchSensor(int nodeAddress) {
	
	
	SensorPayload buffer;
	radio.openWritingPipe(pipes[nodeAddress]);
	radio.openReadingPipe(1,pipes[1]);
	radio.stopListening();
	  
		// Take the time, and send it.  This will block until complete

		//printf("Now sending...");


		bool ok = radio.write( &initialize_cmd, sizeof(int) );

		if (!ok){
				//printf("Failed to write.\n");
				//radio.begin();
				return false;
		}
		// Now, continue listening
		//printf(" ..OK.\n");
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
			//printf("Failed, response timed out.\n");
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

			cout << "***************** " <<  node1_name << " MESSAGE ************************" << endl;
			
			time_t now = time(0);
			tm *ltm = localtime(&now);


			cout << "Receive Time: " << ltm->tm_hour << ":";
			if(ltm->tm_min >= 10){
			cout << ltm->tm_min << ":";
			}			
			else{
			cout << "0" << ltm->tm_min << ":";
			}
			cout << ltm->tm_sec << endl;

			// Spew it
			printf("Temperature: %4.1f \302\260C\n",SensorNode1.sensor1);
			printf("Humidity: %4.1f %%RH\n",SensorNode1.sensor2);
			printf("Air Pressure: %5.1f hPa\n",SensorNode1.sensor3);
			printf("********************************************************* \n");
			printf("\n");
	}
	
	if(node2_enable) {

			cout << "***************** " << node2_name << "  MESSAGE ************************" << endl;
			
			time_t now = time(0);
			tm *ltm = localtime(&now);


			cout << "Receive Time: " << ltm->tm_hour << ":";
			if(ltm->tm_min >= 10){
			cout << ltm->tm_min << ":";
			}			
			else{
			cout << "0" << ltm->tm_min << ":";
			}
			cout << ltm->tm_sec << endl;

			// Spew it
			printf("Temperature: %4.1f \302\260C\n",SensorNode2.sensor1);
			printf("Humidity: %4.1f %%RH\n",SensorNode2.sensor2);
			printf("********************************************************* \n");
			printf("\n");
	}
	last_printout = millis();
}
	

	return;
}

