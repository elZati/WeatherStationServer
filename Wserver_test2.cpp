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
SensorPayload buffer;

// function declaration
bool fetchSensor(int nodeAddress);
void printNodes();
/********** User Config *********/
// Assign a unique identifier for this node, 0 or 1
bool radioNumber = 0;
int initialize_cmd = 12345;
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


 
	radio.openWritingPipe(pipes[0]);
	radio.openReadingPipe(1,pipes[1]);
    radio.printDetails();
	radio.startListening();
	
	// forever loop
	while (1)
	{
	
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

bool fetchSensor(int nodeAddress) {
	
		radio.openWritingPipe(pipes[nodeAddress]);
	  
		// First, stop listening so we can talk.
		radio.stopListening();

		// Take the time, and send it.  This will block until complete

		printf("Now sending...");


		bool ok = radio.write( &initialize_cmd, sizeof(int) );

		if (!ok){
				printf("Failed to write.\n");
				return false;
		}
		// Now, continue listening
		printf(" ..OK.\n");
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
			printf("Failed, response timed out.\n");
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

