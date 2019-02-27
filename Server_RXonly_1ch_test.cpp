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
#define NODE_PRINTOUT_DELAY 5000 //Delay between printing node values 
#define NODE_UPLOAD_DELAY (1000*60*5) //Delay between uploading node values 
#define NODE_SEEN_DELAY (1000*60*15) //Delay between uploading node values 
#define clear() printf("\033[H\033[J")

using namespace std;


RF24 radio(22,0);

struct SensorPayload {
	float sensorID;
	float sensor1;
	float sensor2;
	float sensor3;
	float sensor4;
};

SensorPayload SensorNode1;
SensorPayload SensorNode2;
SensorPayload SensorNode3;

// function declaration
bool fetchSensor2(void);
bool fetchSensor(int nodeAddress);
void printNodes();
void retryFetchSensor(int nodeAddress, int max_attemptCount, int delayTime);
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
int number_of_nodes = 3; //enter amount of active nodes 

bool node1_enable = true;
bool node2_enable = true;
bool node3_enable = true;
bool node4_enable = false;
bool node5_enable = false;

time_t S1;
time_t S2;
time_t S3;

string node1_name = "OUTDOOR SENSOR"; //enter name of node
string node2_name = "INDOOR SENSOR";
string node3_name = "TEST SENSOR";

//***********************************

// Radio pipe addresses for the 2 nodes to communicate.
const uint8_t pipes[][6] = {"1Node","2Node","3node"};

long last_printout = millis();
long last_upload = millis();

long last_seenSensor1 = millis();
long last_seenSensor2 = millis();
long last_seenSensor3 = millis();
long delta_S3 = millis();
long delta_S2 = millis();
long delta_S1 = millis();

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

  // optionally, increase the delay between retries & # of retries
	radio.setRetries(15,15);
	radio.setPALevel(RF24_PA_MIN);
	//radio.setAutoAck(1);
	//radio.enableDynamicPayloads();
	radio.setDataRate(RF24_1MBPS);



	radio.openReadingPipe(1,pipes[0]);
	radio.openWritingPipe(pipes[1]);
	radio.startListening();
    radio.printDetails();

	sleep(1);
	
	// forever loop
	while (1)
	{
		checkNodes();
		
		if(radio.available()){
			fetchSensor2();
		}
		printNodes();	
		uploadData();
		sleep(1);
		if(radio.rxFifoFull ()){
			printf("RX FIFO full! \n");
		}
		

	} // forever loop
	


  return 0;
}
void retryFetchSensor(int nodeAddress, int max_attemptCount, int delayTime) {
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
      printf("Max attempts exceeded for sensor %1d \n",nodeAddress);
    }
  }
}

bool fetchSensor2(void) {
	
	SensorPayload buffer;

		// Grab the response, compare, and send to debugging spew
		
		while(radio.available()){
		radio.read( &buffer, sizeof(SensorPayload) );
		}
		
		radio.stopListening();
		
		if(buffer.sensor1 == 0 && buffer.sensor2 == 0){
		printf("Received (buffer) zero values from sensorID: %4.1f \n",buffer.sensorID);
		while(1){}
		}
			
		if(buffer.sensorID == 0.0)
		{
			delta_S1 = (millis()-last_seenSensor1)/1000;
			SensorNode1 = buffer;
			last_seenSensor1 = millis();
			S1 = time(0);
							
		}
		else if(buffer.sensorID == 2.0)
		{
			delta_S2 = (millis()-last_seenSensor2)/1000;
			SensorNode2 = buffer;
			last_seenSensor2 = millis();
			S2 = time(0);
		}
		else if(buffer.sensorID == 1.0)
		{
			delta_S3 = (millis()-last_seenSensor3)/1000;
			SensorNode3 = buffer;
			last_seenSensor3 = millis();
			S3 = time(0);
			
		}
		else {
			printf("Wrong sensorID received! \n");
			printf("Received sensorID: %4.1f \n",buffer.sensorID);
		}
		
		radio.startListening();


		return true;
	
	
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
				last_seenSensor1 = millis();
				S1 = time(0);
				
			}
			if(nodeAddress == 2)
			{
				SensorNode2 = buffer;
				last_seenSensor2 = millis();
				S2 = time(0);
			}
			if(nodeAddress == 1)
			{
				
				SensorNode3 = buffer;
				last_seenSensor3 = millis();
				S3 = time(0);
			}
			radio.stopListening();
			//radio.begin();
			return true;
		}
	
}

void printNodes(){
	
if (millis()-last_printout > NODE_PRINTOUT_DELAY)
{
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
			printf("********************************************************* \n");
			//printf("\n");
			//printf("SensorNode1 last_seen: %4.1ld \n",last_seenSensor1);
			printf("SensorNode1 delta_RX: %4.1ld \n",delta_S1);
			printf("\n");
	}
	
	if(node2_enable) {

			cout << "***************** " << node2_name << "  MESSAGE ************************" << endl;
			
		
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
			printf("********************************************************* \n");
			//printf("\n");
			//printf("SensorNode2 last_seen: %4.1ld \n",last_seenSensor2);
			printf("SensorNode2 delta_RX: %4.1ld \n",delta_S2);
			printf("\n");
	}
	
	if(node3_enable) {

			cout << "***************** " << node3_name << "  MESSAGE ************************" << endl;
			
			
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
			printf("********************************************************* \n");
			//printf("\n");
			//printf("SensorNode3 last_seen: %4.1ld \n",last_seenSensor3);
			printf("SensorNode3 delta_RX: %4.1ld \n",delta_S3);
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
	
	if(tempBuffer1 == 0 && tempBuffer2 == 0){
		printf("Tried to upload zero values!");
		while(1){}
	}
	
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
	printf("SensorNode1 reset to zero values! \n");
		while(1){}
	}
	
	if (millis()-last_seenSensor2 > NODE_SEEN_DELAY) {
	SensorNode2.sensor1 = 0;
	SensorNode2.sensor2 = 0;
	printf("********************* NODE2 DEAD ************************************ \n");
	}
	if (millis()-last_seenSensor3 > NODE_SEEN_DELAY) {
	SensorNode3.sensor1 = 0;
	SensorNode3.sensor2 = 0;
	printf("********************* NODE3 DEAD ************************************ \n");
	}
}

