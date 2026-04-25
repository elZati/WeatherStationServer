/*
 * =========================================================================
 * GEMINI SERVER - RASPBERRY PI (v13.3 - DATA ALIGNMENT FIX)
 * -------------------------------------------------------------------------
 * Target: Fixes negative/weird readings (Data Misalignment) and 
 * ensures ACK payloads (Sleep Timers) flush correctly.
 * * * KEY ARCHITECTURAL FIXES:
 * 1. #pragma pack(1): Forces the Pi to treat the struct exactly like the 
 * Arduino does, preventing memory "padding" that causes negative numbers.
 * 2. radio.flush_tx(): Clears the hardware queue so new sleep settings 
 * replace old ones immediately instead of getting stuck.
 * 3. Pre-emptive Loading: Pipes are primed with ACK data before listening.
 * =========================================================================
 */

#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <ctime>
#include <stdio.h>
#include <cstring> 
#include <curl/curl.h>
#include "RF24.h"

using namespace std;

// --- System Definitions ---
#define NODE_UPLOAD_DELAY (1000*60*5) // Time between web database uploads
#define NODE_PRINTOUT_DELAY 2000      // Time between terminal refreshes
#define clear() printf("\033[H\033[J") // Terminal escape code to clear screen

/* * PACKED STRUCT (20 BYTES)
 * ------------------------
 * We use #pragma pack(1) to tell the Pi's ARM processor NOT to add 
 * extra "padding" bytes between the int and the floats. Without this,
 * the Pi might try to make the struct 24 bytes, causing Node 3's
 * data to look like gibberish or negative numbers.
 */
#pragma pack(push, 1)
struct SensorPayload {
    int32_t nodeID;    // 4 bytes: ID of the sender (1, 2, or 3) [cite: 1]
    float sensor1;     // 4 bytes: Temperature (HTU21D) 
    float sensor2;     // 4 bytes: Humidity (HTU21D) 
    float sensor3;     // 4 bytes: Pressure (BMP180) [cite: 6]
    float sensor4;     // 4 bytes: Battery Voltage [cite: 6]
};
#pragma pack(pop)

// --- Global Variables ---
RF24 radio(22, 0);          // CE on Pin 22, CSN on Pin 0
SensorPayload nodes[6];     // Data storage for nodes 1 through 5
time_t last_seen[6] = {0};  // Tracks when each node last "checked in"
float sleep_cmds[6] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0}; // Default multipliers

unsigned long last_upload = 0;
unsigned long last_printout = 0;

// --- v13 Hardware Addressing ---
// These addresses must match the Arduino baseAddress + NODE_ID[cite: 4].
const uint64_t pipes[6] = {
    0xABCDABCD00LL, // Pipe 0 (System Reserved)
    0xABCDABCD01LL, // Node 1
    0xABCDABCD02LL, // Node 2
    0xABCDABCD03LL, // Node 3 (The node giving "weird" results)
    0xABCDABCD04LL, // Node 4
    0xABCDABCD05LL  // Node 5
};

/*
 * updateConfigs(): Monitors config.txt for sleep changes from the GUI.
 * If a change is found, it flushes the hardware buffer to prevent 
 * settings from getting "stuck".
 */
void updateConfigs() {
    static uint32_t last_check = 0;
    if (__millis() - last_check < 2000) return;
    last_check = __millis();
    
    FILE *f = fopen("../config.txt", "r");
    if (f) {
        float new_cmds[6];
        // Read the five expected values from the config file
        if (fscanf(f, "%f %f %f %f %f", &new_cmds[1], &new_cmds[2], &new_cmds[3], &new_cmds[4], &new_cmds[5]) == 5) {
            
            bool changed = false;
            for(int i=1; i<=5; i++) {
                if(new_cmds[i] != sleep_cmds[i]) {
                    sleep_cmds[i] = new_cmds[i];
                    changed = true;
                }
            }

            // If a slider was moved in the GUI, reset the Radio's ACK queue
            if (changed) {
                radio.stopListening(); 
                radio.flush_tx();      // Remove stale ACK packets from hardware
                for (int i = 1; i <= 5; i++) {
                    // Reload fresh settings into the response buffer [cite: 24]
                    radio.writeAckPayload(i, &sleep_cmds[i], sizeof(float));
                }
                radio.startListening();
                printf(">>> CONFIG UPDATE: Radio buffers flushed and re-synced.\n");
            }
        }
        fclose(f);
    }
}

/*
 * saveForUI(): Writes the most recent data to JSON for the Web interface.
 */
void saveForUI() {
    FILE *f = fopen("../live_data.json", "w");
    if (!f) return;
    fprintf(f, "[\n");
    bool first = true;
    for (int i = 1; i <= 5; i++) {
        if (last_seen[i] > 0) {
            if (!first) fprintf(f, ",\n");
            fprintf(f, "{\"id\": %d, \"temp\": %.2f, \"hum\": %.2f, \"batt\": %.2f, \"last_seen\": %lld}",
                    i, nodes[i].sensor1, nodes[i].sensor2, nodes[i].sensor4, (long long)last_seen[i]);
            first = false;
        }
    }
    fprintf(f, "\n]");
    fclose(f);
}

/*
 * printNodes(): Display the sensor dashboard in the Linux terminal.
 */
void printNodes() {
    clear();
    printf("===== SÄÄ-SERVER 1.0 =====\n");
    printf("Listening on Pipes 1-5 | Data Pack: 20-Bytes\n");
    printf("--------------------------------------------------\n");
    for (int i = 1; i <= 5; i++) {
        if (last_seen[i] > 0) {
            tm *ltm = localtime(&last_seen[i]);
            printf("NODE %d [%02d:%02d:%02d] | T: %5.1fC | H: %5.1f%% | P: %4.0f | B: %1.2fV | CMD: %.0f\n", 
                    i, ltm->tm_hour, ltm->tm_min, ltm->tm_sec,
                    nodes[i].sensor1, nodes[i].sensor2, nodes[i].sensor3, nodes[i].sensor4, sleep_cmds[i]);
        } else {
            printf("NODE %d | Offline\n", i);
        }
    }
}

/*
 * uploadData(): Push local sensor values to the remote MySQL database.
 */
void uploadData() {
    if (last_seen[1] == 0 && last_seen[2] == 0) return;
    CURL *curl;
    CURLcode res;
    char url[512];
    
    // Construct the PHP GET request with current readings
    sprintf(url, "http://www.rxtx-designs.com/saa/upload_values.php?tempin=%.2f&temp=%.2f&humin=%.2f&hum=%.2f&press=%.2f",
            nodes[2].sensor1, nodes[1].sensor1, nodes[2].sensor2, nodes[1].sensor2, nodes[1].sensor3);
            
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // Don't hang if internet is down
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
}

int main(int argc, char** argv) {
    // Initialize Radio
    if (!radio.begin()) {
        printf("CRITICAL: SPI Radio connection failed.\n");
        return 1;
    }

    // Configure Radio to match Arduinos exactly [cite: 15]
    radio.setPALevel(RF24_PA_HIGH);
    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16); 
    radio.enableAckPayload();      // Enable "Talk-back" feature
    radio.enableDynamicPayloads(); // Required for varying ACK data

    radio.stopListening();
    radio.closeReadingPipe(0); // Pipe 0 used for management

    // Open reading pipes for Nodes 1 through 5
    for (int i = 1; i <= 5; i++) {
        radio.openReadingPipe(i, pipes[i]);
    }

    // Initial config load and buffer prime
    updateConfigs();
    for (int i = 1; i <= 5; i++) {
        radio.writeAckPayload(i, &sleep_cmds[i], sizeof(float));
    }

    radio.startListening();
    last_upload = __millis();

    while (1) {
        updateConfigs();
        
        uint8_t pipeNum;
        if (radio.available(&pipeNum)) {
            SensorPayload incoming;
            // Read exactly 20 bytes from the radio [cite: 23]
            radio.read(&incoming, sizeof(incoming));
            
            int id = incoming.nodeID; // Retrieve ID from the first 4 bytes 
            
            if (id >= 1 && id <= 5) {
                // Update local memory with fresh data
                nodes[id] = incoming;
                last_seen[id] = time(NULL);
                
                // Immediately reload the ACK for this specific pipe 
                // so the node hears the Pi on the NEXT wake cycle [cite: 24]
                radio.writeAckPayload(pipeNum, &sleep_cmds[id], sizeof(float));
                
                saveForUI(); // Refresh the web GUI JSON file
            }
        }

        // Dashboard Refresh
        if (__millis() - last_printout > NODE_PRINTOUT_DELAY) {
            printNodes();
            last_printout = __millis();
        }

        // Web Upload
        if (__millis() - last_upload > NODE_UPLOAD_DELAY) {
            uploadData();
            last_upload = __millis();
        }

        usleep(1000); // Prevent CPU from pinning at 100%
    }
    return 0;
}
