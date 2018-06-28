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
#define NODE_SEEN_DELAY (1000*60*1) //Delay between uploading node values 
#define clear() printf("\033[H\033[J")

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

SensorPayload SensorNode1_avg_temp;
SensorPayload SensorNode2_avg_temp;
SensorPayload SensorNode1_avg;
SensorPayload SensorNode2_avg;

int node1_count = 0;
int node2_count = 0;

// function declaration
bool fetchSensor(int nodeAddress);
void printNodes();
void retryFetchSensor(int nodeAddress, int max_attemptCount, int delayTime);
void uploadData(void);
void retTemperature(char *buff, float value);
void retHumidity(char *buff, float value);
void retPressure(char *buff, float value);
void checkNodes(void);
void storeReading(struct SensorPayload data, int node);
void clearAvgs(void);
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

time_t sensor1_received = 0;
time_t sensor2_received = 0;

//***********************************

// Radio pipe addresses for the 2 nodes to communicate.
const uint8_t pipes[][6] = {"1Node","2Node","3node"};

long last_printout = millis();
long last_upload = millis();

long last_seenSensor1 = millis();
long last_seenSensor2 = millis();

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

	//optionally, increase the delay between retries & # of retries
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
		checkNodes();
 		retryFetchSensor(0, 10, 0.1);
		retryFetchSensor(2, 10, 0.1);
		sleep(1);
		printNodes();	
		uploadData();
		

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
      //printf("Max attempts exceeded for sensor %1d \n",nodeAddress);
    }
  }
  if(ok && !max_attempts)
  {
	 if(nodeAddress==0){
			sensor1_received = time(0);
			storeReading(SensorNode1, 1);
	} 
	if(nodeAddress==2){
			sensor2_received = time(0);
			storeReading(SensorNode2, 2);
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
				last_seenSensor1 = millis();
			}
			if(nodeAddress == 2)
			{
				SensorNode2 = buffer;
				last_seenSensor2 = millis();
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
			printf("Average reading for Node1 Temperature: %1.1f \302\260C\n", SensorNode1_avg.sensor1);
			printf("\n");
	}
	
	if(node2_enable) {

			cout << "***************** " << node2_name << "  MESSAGE ************************" << endl;
			

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
			printf("Average reading for Node2 Temperature: %1.1f \302\260C\n", SensorNode2_avg.sensor1);
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
	clearAvgs();
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
	//SensorNode1.sensor1 = 0;
	//SensorNode1.sensor2 = 0;
	//SensorNode1.sensor3 = 0;
	printf("********************* NODE1 DEAD ************************************ \n");
	}
	
	if (millis()-last_seenSensor2 > NODE_SEEN_DELAY) {
	//SensorNode2.sensor1 = 0;
	//SensorNode2.sensor2 = 0;
	printf("********************* NODE2 DEAD ************************************ \n");
	}
}


void storeReading(struct SensorPayload data, int node) {

	if (!data.sensor1 == 0 && !data.sensor2 == 0 && !data.sensor3 == 0 && !data.sensor4 == 0){

	if (node == 1){
		SensorNode1_avg_temp.sensor1 += data.sensor1;
		SensorNode1_avg_temp.sensor2 += data.sensor2;
		SensorNode1_avg_temp.sensor3 += data.sensor3;
		SensorNode1_avg_temp.sensor4 += data.sensor4;
		node1_count++;

		SensorNode1_avg.sensor1 = SensorNode1_avg_temp.sensor1 / node1_count;
		SensorNode1_avg.sensor2 = SensorNode1_avg_temp.sensor2 / node1_count;
		SensorNode1_avg.sensor3 = SensorNode1_avg_temp.sensor3 / node1_count;
		SensorNode1_avg.sensor4 = SensorNode1_avg_temp.sensor4 / node1_count;

		//printf("Average reading for Node1 Temperature: %1.1f \302\260C\n", SensorNode1_avg.sensor1);


	}

	if (node == 2){
		SensorNode2_avg_temp.sensor1 += data.sensor1;
		SensorNode2_avg_temp.sensor2 += data.sensor2;
		SensorNode2_avg_temp.sensor3 += data.sensor3;
		SensorNode2_avg_temp.sensor4 += data.sensor4;
		node2_count++;

		SensorNode2_avg.sensor1 = SensorNode2_avg_temp.sensor1 / node2_count;
		SensorNode2_avg.sensor2 = SensorNode2_avg_temp.sensor2 / node2_count;
		SensorNode2_avg.sensor3 = SensorNode2_avg_temp.sensor3 / node2_count;
		SensorNode2_avg.sensor4 = SensorNode2_avg_temp.sensor4 / node2_count;

		//printf("Average reading for Node2 Temperature: %1.1f \302\260C\n", SensorNode2_avg.sensor1);
	}
	}
	
}

void clearAvgs(void){

	node1_count = 0;
	node2_count = 0;

	SensorNode1_avg_temp.sensor1 = 0;
	SensorNode1_avg_temp.sensor2 = 0;
	SensorNode1_avg_temp.sensor3 = 0;
	SensorNode1_avg_temp.sensor4 = 0;

	SensorNode2_avg_temp.sensor1 = 0;
	SensorNode2_avg_temp.sensor2 = 0;
	SensorNode2_avg_temp.sensor3 = 0;
	SensorNode2_avg_temp.sensor4 = 0;

	SensorNode1_avg.sensor1 = 0;
	SensorNode1_avg.sensor2 = 0;
	SensorNode1_avg.sensor3 = 0;
	SensorNode1_avg.sensor4 = 0;

	SensorNode2_avg.sensor1 = 0;
	SensorNode2_avg.sensor2 = 0;
	SensorNode2_avg.sensor3 = 0;
	SensorNode2_avg.sensor4 = 0;
}

