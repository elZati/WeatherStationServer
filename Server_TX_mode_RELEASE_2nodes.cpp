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
#include <stdio.h>
#include <curl/curl.h>

#define NODE_RETRY 3 // Number of radio retries per node
#define NODE_TIMEOUT 300 // Timeout value for radio messaging
#define NODE_PRINTOUT_DELAY 2000 //Delay between printing node values 
#define NODE_UPLOAD_DELAY (1000*60*5) //Delay between uploading node values 
#define NODE_SEEN_DELAY (1000*60*15) //Delay between uploading node values 
#define clear() printf("\033[H\033[J")
#define DEFAULT_SLEEP_PERIOD_SENSOR3 80
#define DEFAULT_SLEEP_PERIOD_SENSOR2 12
#define DEFAULT_SLEEP_PERIOD_SENSOR1 16

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

// function declaration
bool fetchSensor(int nodeAddress);
void printNodes();
bool retryFetchSensor(int nodeAddress, int max_attemptCount, int delayTime);
void uploadData(void);
void retTemperature(char *buff, float value);
void retHumidity(char *buff, float value);
void retPressure(char *buff, float value);
void checkNodes(void);
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

time_t S1;
time_t S2;
time_t S3;

string  node1_name = "OUTDOOR SENSOR"; //enter name of node
string node2_name = "INDOOR SENSOR";
string node3_name = "TEST SENSOR";

//***********************************

// Radio pipe addresses for the 2 nodes to communicate.
const uint8_t pipes[][6] = {"1Node","2Node","3node","4node"};

unsigned long last_printout = millis();
unsigned long last_upload = millis();

unsigned long last_seenSensor1 = millis();
unsigned long last_seenSensor2 = millis();
unsigned long last_seenSensor3 = millis();

unsigned long delta_S3 = 0;
unsigned long delta_S2 = 0;
unsigned long delta_S1 = 0;

bool node3_lost = true;
bool node2_lost = true;
bool node1_lost = true;

float SLEEP_PERIOD_SENSOR1 = DEFAULT_SLEEP_PERIOD_SENSOR1;
float SLEEP_PERIOD_SENSOR2 = DEFAULT_SLEEP_PERIOD_SENSOR2;
float SLEEP_PERIOD_SENSOR3 = DEFAULT_SLEEP_PERIOD_SENSOR3;

int main(int argc, char** argv){
	clear();
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
	radio.setRetries(2,15);
	radio.setPALevel(RF24_PA_HIGH);
	radio.setDataRate(RF24_250KBPS);
	radio.openReadingPipe(1,pipes[1]);
    radio.printDetails();
	radio.startListening();
	radio.powerUp();
	
	sleep(1);
	
	// forever loop
	while (1)
	{
		
		//if(millis()-last_seenSensor3 >= (SLEEP_PERIOD_SENSOR3*8*1000+3000) || node3_lost){
 		//node3_lost = retryFetchSensor(3, 8, 0.25);
		//}
			
		if(millis()-last_seenSensor2 >= (SLEEP_PERIOD_SENSOR2*8*1000+2000) || node2_lost){
 		node2_lost = retryFetchSensor(2, 8, 0.25);
		}
		
		if(millis()-last_seenSensor1 >= (SLEEP_PERIOD_SENSOR1*8*1000+1000) || node1_lost){
 		node1_lost = retryFetchSensor(0, 8, 0.25);
		}
	
		sleep(1);
		printNodes();	
		uploadData();

	} // forever loop
	


  return 0;
}
bool retryFetchSensor(int nodeAddress, int max_attemptCount, int delayTime) {
//printf("Fetching.. %1d \n",nodeAddress);
  bool max_attempts = false;
  int att_counter = 0;
  bool ok = false;

  while(!ok && !max_attempts){
    sleep(delayTime);
    ok = fetchSensor(nodeAddress);
    att_counter++;
	
    if(att_counter > max_attemptCount){
      max_attempts = true;
      //printf("Max attempts exceeded for sensor %1d \n",nodeAddress);
	  return true;
    }
  }
  return false;
}

bool fetchSensor(int nodeAddress) {
	
	SensorPayload buffer;
	radio.openWritingPipe(pipes[nodeAddress]);
	radio.openReadingPipe(1,pipes[1]);
	radio.stopListening();
	float sleep_period = 999;
	if(nodeAddress == 3){
		sleep_period = SLEEP_PERIOD_SENSOR3;
	}else if (nodeAddress == 2){
		sleep_period = SLEEP_PERIOD_SENSOR2;
	}else{
		sleep_period = SLEEP_PERIOD_SENSOR1;
	}
		
	  
		// Take the time, and send it.  This will block until complete

		//printf("Now sending...");


		bool ok = radio.write( &sleep_period, sizeof(float) );

		if (!ok){
				//printf("Failed to write, NodeAddress %1d \n",nodeAddress);
				//radio.begin();
				return false;
		}
		// Now, continue listening
		//printf(" ..OK.\n");
		radio.startListening();
		radio.flush_rx();

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
			//printf("Failed, response timed out, NodeAddress %1d \n",nodeAddress);
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
				delta_S1 = (millis()-last_seenSensor1)/1000;
				SensorNode1 = buffer;
				last_seenSensor1 = millis();
				S1 = time(0);
			}
			if(nodeAddress == 2)
			{
				delta_S2 = (millis()-last_seenSensor2)/1000;
				SensorNode2 = buffer;
				last_seenSensor2 = millis();
				S2 = time(0);
			}
			if(nodeAddress == 3)
			{
				delta_S3 = (millis()-last_seenSensor3)/1000;
				SensorNode3 = buffer;
				last_seenSensor3 = millis();
				S3 = time(0);
			}
			radio.stopListening();
			//radio.begin();
			radio.flush_rx();
			
			return true;
		}
	
}

void printNodes(){
	
if (millis()-last_printout > NODE_PRINTOUT_DELAY){
	clear();
	
	if(node1_enable) {

			cout << "***************** " <<  node1_name << " MESSAGE ************************" << endl;
			
			tm *ltm = localtime(&S1);


			cout << "Receive Time: " << ltm->tm_hour << ":";
			if(ltm->tm_min >= 10){
			cout << ltm->tm_min << ":";
			}			
			else{
			cout << "0" << ltm->tm_min << ":";
			}
			cout << ltm->tm_sec << endl;

			// Spew it
			printf("Temperature: %4.1f \260C\n",SensorNode1.sensor1);
			printf("Humidity: %4.1f %%RH\n",SensorNode1.sensor2);
			printf("Air Pressure: %5.1f hPa\n",SensorNode1.sensor3);
			printf("SensorNode1 delta_RX: %4.1lu \n",delta_S1);
			printf("********************************************************* \n");				
			if(node1_lost){
			printf("Node 1 lost, searching.. \n");
			}
			printf("\n");
		
	}
	
	if(node2_enable) {

			cout << "***************** " << node2_name << " MESSAGE ************************" << endl;
			
			tm *ltm = localtime(&S2);


			cout << "Receive Time: " << ltm->tm_hour << ":";
			if(ltm->tm_min >= 10){
			cout << ltm->tm_min << ":";
			}			
			else{
			cout << "0" << ltm->tm_min << ":";
			}
			cout << ltm->tm_sec << endl;

			// Spew it
			printf("Temperature: %4.1f \260C\n",SensorNode2.sensor1);
			printf("Humidity: %4.1f %%RH\n",SensorNode2.sensor2);
			printf("SensorNode2 delta_RX: %4.1lu \n",delta_S2);
			printf("********************************************************* \n");
			if(node2_lost){
			printf("Node 2 lost, searching.. \n");
			}
			printf("\n");
			
			
			
	}
	
	if(node3_enable) {

			cout << "***************** " << node3_name << " MESSAGE ************************" << endl;
			

			tm *ltm = localtime(&S3);

			cout << "Receive Time: " << ltm->tm_hour << ":";
			if(ltm->tm_min >= 10){
			cout << ltm->tm_min << ":";
			}			
			else{
			cout << "0" << ltm->tm_min << ":";
			}
			cout << ltm->tm_sec << endl;

			// Spew it
			printf("Temperature: %4.1f \260C\n",SensorNode3.sensor1);
			printf("Humidity: %4.1f %%RH\n",SensorNode3.sensor2);
			printf("SensorNode3 delta_RX: %4.1lu \n",delta_S3);
			printf("********************************************************* \n");
			if(node3_lost){
			printf("Node 3 lost, searching.. \n");
			}
			printf("\n");
	}
	
	last_printout = millis();
}
	

	return;
}
	

void uploadData(void) {
	
if (millis()-last_upload > NODE_UPLOAD_DELAY) {
	char tempBuffer1[80]; //Don't change the buffer size.
	char tempBuffer2[80]; //Don't change the buffer size.
	char humBuffer1[80]; //Don't change the buffer size.
	char humBuffer2[80]; //Don't change the buffer size.
	char pressBuffer1[80]; //Don't change the buffer size.
	
	retTemperature(tempBuffer1, SensorNode1.sensor1);
	retTemperature(tempBuffer2, SensorNode2.sensor1);
	
	retHumidity(humBuffer1, SensorNode1.sensor2);
	retHumidity(humBuffer2, SensorNode2.sensor2);
	
	retPressure(pressBuffer1, SensorNode1.sensor3);
	
	char str[200];
	stpcpy(str,"http://www.rxtx-designs.com/saa/upload_values.php?tempin=");
	strcat(str,tempBuffer2);	
	strcat(str,"&temp=");
	strcat(str,tempBuffer1);
	strcat(str,"&humin=");
	strcat(str,humBuffer2);
	strcat(str,"&hum=");
	strcat(str,humBuffer1);
	strcat(str,"&press=");
	strcat(str,pressBuffer1);
	puts (str);

	CURL *curl;
	CURLcode res;

	curl_global_init(CURL_GLOBAL_DEFAULT);

	curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, str);

#ifdef SKIP_PEER_VERIFICATION
		/*
		* If you want to connect to a site who isn't using a certificate that is
		* signed by one of the certs in the CA bundle you have, you can skip the
		* verification of the server's certificate. This makes the connection
		* A LOT LESS SECURE.
		*
		* If you have a CA cert for the server stored someplace else than in the
		* default bundle, then the CURLOPT_CAPATH option might come handy for
		* you.
		*/
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
#endif

#ifdef SKIP_HOSTNAME_VERIFICATION
		/*
		* If the site you're connecting to uses a different host name that what
		* they have mentioned in their server certificate's commonName (or
		* subjectAltName) fields, libcurl will refuse to connect. You can skip
		* this check, but this will make the connection less secure.
		*/
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
#endif

		/* Perform the request, res will get the return code */
		res = curl_easy_perform(curl);
		/* Check for errors */
		if (res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n",
			curl_easy_strerror(res));

		/* always cleanup */
		curl_easy_cleanup(curl);
	}

	curl_global_cleanup();
	last_upload = millis();
	
/* 	if(!node1_lost){			//Seems to cause issues 18.9.2019
	SLEEP_PERIOD_SENSOR1 = 70;
	}
	if(!node2_lost){
	SLEEP_PERIOD_SENSOR2 = 70;
	}
	if(!node3_lost){
	SLEEP_PERIOD_SENSOR3 = 70;
	} */
	
	
	}


	
	return;

}

void retTemperature(char *buff, float value){
	
	if(value >= 0){
	snprintf(buff,80,"%1.1f",value);
	}else{
	snprintf(buff,80,"%1.1f",value);
	}	
}

void retPressure(char *buff, float value){
	if(value >= 1000){
	snprintf(buff,80,"%1.1f",value);
	}else{
	snprintf(buff,80,"%1.1f",value);
	}	
}

void retHumidity(char *buff, float value){
	if(value >= 100){
	snprintf(buff,80,"%1.1f",value);
	}else{
	snprintf(buff,80,"%1.1f",value);
	}	
}

void checkNodes(void){
	
	if (millis()-last_seenSensor1 > NODE_SEEN_DELAY) {
	SensorNode1.sensor1 = 0;
	SensorNode1.sensor2 = 0;
	SensorNode1.sensor3 = 0;
	printf("********************* NODE1 DEAD ************************************ \n");
	}
	
	if (millis()-last_seenSensor2 > NODE_SEEN_DELAY) {
	SensorNode2.sensor1 = 0;
	SensorNode2.sensor2 = 0;
	printf("********************* NODE2 DEAD ************************************ \n");
	}
}

