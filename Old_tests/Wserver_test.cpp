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
#define NODE1_REFRESH_DELAY 1000 //Delay between polling node 1
#define NODE2_REFRESH_DELAY 5000 //Delay between polling node 2
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
SensorPayload SensorNode3;
SensorPayload SensorNode4;
SensorPayload SensorNode5;

// function declaration
bool fetchSensor(const uint8_t *sensorAddress);
void printNodes();
bool fetchNode(int nodeNumber);

/********** User Config *********/
// Assign a unique identifier for this node, 0 or 1
bool radioNumber = 0;
int initialize_cmd = 12345;
/********************************/

// Radio pipe addresses for the 2 nodes to communicate.
const uint8_t pipes[][6] = {"1Node","2Node","3node","4node","5node"};
string nodes[] = {"SensorNode1","SensorNode2"};

//********* Sensor Config ***********
int number_of_nodes = 2; //enter amount of active nodes 

bool node1_enable = true;
bool node2_enable = false;
bool node3_enable = false;
bool node4_enable = false;
bool node5_enable = false;

string  node1_name = "OUTDOOR SENSOR"; //enter name of node
string node2_name = "INDOOR SENSOR";
 

//***********************************

long last_printout = millis();
long last_node1_refresh = millis();
long last_node2_refresh = millis();

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
  // Dump the configuration of the rf unit for debugging
  //radio.printDetails();

 
	radio.openWritingPipe(pipes[0]);
	radio.openReadingPipe(1,pipes[1]);
        radio.printDetails();
	radio.startListening();
	
	// forever loop
	while (1)
	{
	
			//fetchSensor(pipes[0]);
			//printNodes();
			sleep(1);
			fetchNode(0);
			printNodes();
			//sleep(3);


	} // forever loop
	


  return 0;
}

bool fetchSensor(const uint8_t *sensorAddress) {
	
		radio.openWritingPipe(sensorAddress);
		//radio.openReadingPipe(1,pipes[0]);
	  
		// First, stop listening so we can talk.
		radio.stopListening();

		// Take the time, and send it.  This will block until complete

		printf("Now sending...");


		bool ok = radio.write( &initialize_cmd, sizeof(int) );

		if (!ok){
				printf("failed.\n");
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
			radio.read( &SensorNode1, sizeof(SensorPayload) );
			return true;
		}
	
}

bool fetchNode(int nodeNumber) {
	
		radio.openWritingPipe(pipes[nodeNumber]);

	  
		// First, stop listening so we can talk.
		radio.stopListening();

		// Take the time, and send it.  This will block until complete

		//printf("Now sending...");
		bool all_ok = false;
		int retry_counter = 0;
		
		while(!all_ok && retry_counter <= NODE_RETRY){
		bool ok = radio.write( &initialize_cmd, sizeof(int) );

		if (!ok){
				printf(" Write failed.\n");
		} else{
		// Now, continue listening
		//printf(" ..OK.\n");
		radio.startListening();
		}
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

		} if(!timeout){
		all_ok = true;
                //printf("All OK.\n");

		}
	}


		if(all_ok)
		{

                 //printf("Grabbin' response..\n");
			if(nodeNumber == 0){
			// Grab the response, compare, and send to debugging spew
			radio.read( &SensorNode1, sizeof(SensorPayload) );
			}
                        if(nodeNumber == 2){
                        // Grab the response, compare, and send to debugging spew
                        radio.read( &SensorNode2, sizeof(SensorPayload) );
                        }

			return true;
		} 

		else {
			return false;
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

