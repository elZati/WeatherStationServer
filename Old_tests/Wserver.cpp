/*
 Weather Station Server for RPi

 */
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include "RF24.h"


using namespace std;


RF24 radio(22,0);

struct SensorPayload {
	float sensor1;
	float sensor2;
	float sensor3;
	float sensor4;
};

SensorPayload SensorNode1;

time_t timev;

/********** User Config *********/
// Assign a unique identifier for this node, 0 or 1
bool radioNumber = 0;
int initialize_cmd = 12345;
float sensor1 = 0;
/********************************/

// Radio pipe addresses for the 2 nodes to communicate.
const uint8_t pipes[][6] = {"1Node","2Node"};


int main(int argc, char** argv){

  cout << "Weather Station Server for RPi";
  time(&timev);
  cout << timev;
  // Setup and configure rf radio
  radio.begin();

  // optionally, increase the delay between retries & # of retries
  radio.setRetries(15,15);
  // Dump the configuration of the rf unit for debugging
  radio.printDetails();

    if ( !radioNumber )    {
      radio.openWritingPipe(pipes[0]);
      radio.openReadingPipe(1,pipes[1]);
    } else {
      radio.openWritingPipe(pipes[1]);
      radio.openReadingPipe(1,pipes[0]);
    }
	
	radio.startListening();
	
	// forever loop
	while (1)
	{
	
			// First, stop listening so we can talk.
			radio.stopListening();

			// Take the time, and send it.  This will block until complete

			printf("Now sending...\n");


			bool ok = radio.write( &initialize_cmd, sizeof(int) );

			if (!ok){
				printf("failed.\n");
			}
			// Now, continue listening
			radio.startListening();

			// Wait here until we get a response, or timeout (250ms)
			unsigned long started_waiting_at = millis();
			bool timeout = false;
			while ( ! radio.available() && ! timeout ) {
				if (millis() - started_waiting_at > 200 )
					timeout = true;
			}


			// Describe the results
			if ( timeout )
			{
				printf("Failed, response timed out.\n");
			}
			else
			{
				// Grab the response, compare, and send to debugging spew
				
				radio.read( &SensorNode1, sizeof(SensorPayload) );

				// Spew it
				printf("Got response %5.2f\n",SensorNode1.sensor1);
			}
			sleep(1);
		


	} // forever loop

  return 0;
}

