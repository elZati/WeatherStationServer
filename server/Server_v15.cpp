/*
 * =========================================================================
 * GEMINI SERVER - RASPBERRY PI (v15.0 - HW 2.0 AIR QUALITY SUPPORT)
 * -------------------------------------------------------------------------
 * Changes from v13.3:
 *  - Batch upload (one POST per 15-min cycle instead of per-node GET)
 *  - Sensor HW 2.0 support: 25-byte SensorPayloadV2 (eCO2, TVOC, AQI)
 *    Legacy 20-byte nodes continue to work with no changes required.
 *    The server distinguishes payload version by dynamic payload size.
 * =========================================================================
 */

#include <cstdlib>
#include <cstdint>
#include <cstdarg>
#include <iostream>
#include <unistd.h>
#include <ctime>
#include <stdio.h>
#include <cstring>
#include <sys/stat.h>
#include <curl/curl.h>
#include "RF24.h"

using namespace std;

// --- System Definitions ---
#define NODE_UPLOAD_DELAY  (1000*60*15)  // Web upload period (ms)
#define NODE_PRINTOUT_DELAY 2000         // Terminal refresh period (ms)
#define clear() printf("\033[H\033[J")
#define LOG_FILE   "../server.log"
#define LOG_BACKUP "../server.log.1"
#define LOG_MAX_BYTES (2 * 1024 * 1024)  // rotate at 2 MB

// =========================================================================
// Logging — timestamped append to server.log; rotates at 2 MB.
// =========================================================================
static void log_msg(const char *level, const char *fmt, ...) {
    struct stat st;
    if (stat(LOG_FILE, &st) == 0 && st.st_size > LOG_MAX_BYTES)
        rename(LOG_FILE, LOG_BACKUP);

    FILE *f = fopen(LOG_FILE, "a");
    if (!f) return;

    time_t now = time(NULL);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(f, "[%s] [%s] ", ts, level);

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

// =========================================================================
// PAYLOAD STRUCTS
// IMPORTANT: keep in sync with Arduino firmware structs.
// =========================================================================
#pragma pack(push, 1)

// Legacy 20-byte payload (all pre-HW2 nodes)
struct SensorPayload {
    int32_t nodeID;
    float   sensor1;   // temperature
    float   sensor2;   // humidity
    float   sensor3;   // pressure
    float   sensor4;   // battery voltage
};

// HW 2.0 extended 25-byte payload (ESP32-C3 + BME280 + ENS160)
struct SensorPayloadV2 {
    int32_t  nodeID;
    float    sensor1;  // temperature
    float    sensor2;  // humidity
    float    sensor3;  // pressure
    float    sensor4;  // 0.0 (USB powered)
    uint16_t eco2;     // eCO2 ppm
    uint16_t tvoc;     // TVOC ppb
    uint8_t  aqi;      // AQI 1–5  (0 = warming up)
};

#pragma pack(pop)

// Extra fields only present for HW 2.0 nodes
struct NodeExtras {
    uint16_t eco2  = 0;
    uint16_t tvoc  = 0;
    uint8_t  aqi   = 0;
    bool     is_v2 = false;
};

// --- Global State ---
RF24        radio(22, 0);
SensorPayload nodes[6];
NodeExtras    extras[6];
time_t        last_seen[6]  = {0};
float         sleep_cmds[6] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};

unsigned long last_upload   = 0;
unsigned long last_printout = 0;

const uint64_t pipes[6] = {
    0xABCDABCD00LL,  // Pipe 0 (reserved)
    0xABCDABCD01LL,  // Node 1
    0xABCDABCD02LL,  // Node 2
    0xABCDABCD03LL,  // Node 3
    0xABCDABCD04LL,  // Node 4
    0xABCDABCD05LL   // Node 5
};

// =========================================================================
// updateConfigs(): Watches config.txt for sleep-timer changes from the GUI.
// =========================================================================
void updateConfigs() {
    static uint32_t last_check = 0;
    if (__millis() - last_check < 2000) return;
    last_check = __millis();

    FILE *f = fopen("../config.txt", "r");
    if (f) {
        float new_cmds[6];
        if (fscanf(f, "%f %f %f %f %f",
                   &new_cmds[1], &new_cmds[2], &new_cmds[3],
                   &new_cmds[4], &new_cmds[5]) == 5) {
            bool changed = false;
            for (int i = 1; i <= 5; i++) {
                if (new_cmds[i] != sleep_cmds[i]) {
                    sleep_cmds[i] = new_cmds[i];
                    changed = true;
                }
            }
            if (changed) {
                radio.stopListening();
                radio.flush_tx();
                for (int i = 1; i <= 5; i++)
                    radio.writeAckPayload(i, &sleep_cmds[i], sizeof(float));
                radio.startListening();
                printf(">>> CONFIG UPDATE: Radio buffers flushed and re-synced.\n");
                log_msg("INFO", "Config update: sleep=[%.0f %.0f %.0f %.0f %.0f]",
                        sleep_cmds[1], sleep_cmds[2], sleep_cmds[3],
                        sleep_cmds[4], sleep_cmds[5]);
            }
        }
        fclose(f);
    }
}

// =========================================================================
// saveForUI(): Writes live_data.json for the Pi GUI.
//   HW 2.0 nodes include eco2/tvoc/aqi fields; legacy nodes do not.
// =========================================================================
void saveForUI() {
    FILE *f = fopen("../live_data.json", "w");
    if (!f) return;
    fprintf(f, "[\n");
    bool first = true;
    for (int i = 1; i <= 5; i++) {
        if (last_seen[i] == 0) continue;
        if (!first) fprintf(f, ",\n");
        if (extras[i].is_v2) {
            fprintf(f, "{\"id\":%d,\"temp\":%.2f,\"hum\":%.2f,\"press\":%.2f,"
                       "\"batt\":%.2f,\"eco2\":%u,\"tvoc\":%u,\"aqi\":%u,"
                       "\"last_seen\":%lld}",
                    i, nodes[i].sensor1, nodes[i].sensor2,
                    nodes[i].sensor3, nodes[i].sensor4,
                    extras[i].eco2, extras[i].tvoc, extras[i].aqi,
                    (long long)last_seen[i]);
        } else {
            fprintf(f, "{\"id\":%d,\"temp\":%.2f,\"hum\":%.2f,\"press\":%.2f,"
                       "\"batt\":%.2f,\"last_seen\":%lld}",
                    i, nodes[i].sensor1, nodes[i].sensor2,
                    nodes[i].sensor3, nodes[i].sensor4,
                    (long long)last_seen[i]);
        }
        first = false;
    }
    fprintf(f, "\n]");
    fclose(f);
}

// =========================================================================
// printNodes(): Terminal dashboard.
// =========================================================================
void printNodes() {
    clear();
    printf("===== SÄÄ-SERVER 1.0 =====\n");
    printf("Listening on Pipes 1-5 | V1=20B  V2=25B\n");
    printf("--------------------------------------------------\n");
    for (int i = 1; i <= 5; i++) {
        if (last_seen[i] > 0) {
            tm *ltm = localtime(&last_seen[i]);
            if (extras[i].is_v2) {
                printf("NODE %d [%02d:%02d:%02d] HW2 | T:%5.1fC H:%5.1f%% P:%4.0f "
                       "| eCO2:%4uppm TVOC:%4uppb AQI:%u | CMD:%.0f\n",
                       i, ltm->tm_hour, ltm->tm_min, ltm->tm_sec,
                       nodes[i].sensor1, nodes[i].sensor2, nodes[i].sensor3,
                       extras[i].eco2, extras[i].tvoc, extras[i].aqi,
                       sleep_cmds[i]);
            } else {
                printf("NODE %d [%02d:%02d:%02d]     | T:%5.1fC H:%5.1f%% P:%4.0f "
                       "| B:%1.2fV | CMD:%.0f\n",
                       i, ltm->tm_hour, ltm->tm_min, ltm->tm_sec,
                       nodes[i].sensor1, nodes[i].sensor2, nodes[i].sensor3,
                       nodes[i].sensor4, sleep_cmds[i]);
            }
        } else {
            printf("NODE %d | Offline\n", i);
        }
    }
}

// =========================================================================
// http_post_json(): Fire-and-forget HTTP POST with JSON body.
// =========================================================================
static size_t discard_cb(void *, size_t size, size_t n, void *) { return size * n; }

static void http_post_json(const char *url, const char *body) {
    CURL *curl = curl_easy_init();
    if (!curl) { log_msg("ERROR", "curl_easy_init failed"); return; }
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_cb);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (res != CURLE_OK)
        log_msg("ERROR", "Upload failed: %s", curl_easy_strerror(res));
    else if (http_code != 200)
        log_msg("WARN",  "Upload HTTP %ld (unexpected)", http_code);
    else
        log_msg("INFO",  "Upload OK (HTTP 200)");
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

// =========================================================================
// uploadData(): Batches all active nodes into one JSON POST.
//   HW 2.0 nodes include eco2/tvoc/aqi; legacy nodes do not.
// =========================================================================
void uploadData() {
    char body[1024] = "[";
    bool first = true;
    for (int i = 1; i <= 5; i++) {
        if (last_seen[i] == 0) continue;
        char entry[192];
        if (extras[i].is_v2) {
            snprintf(entry, sizeof(entry),
                "%s{\"node_id\":%d,\"temp\":%.2f,\"hum\":%.2f,\"press\":%.2f,"
                "\"batt\":%.2f,\"eco2\":%u,\"tvoc\":%u,\"aqi\":%u}",
                first ? "" : ",",
                i, nodes[i].sensor1, nodes[i].sensor2,
                nodes[i].sensor3, nodes[i].sensor4,
                extras[i].eco2, extras[i].tvoc, extras[i].aqi);
        } else {
            snprintf(entry, sizeof(entry),
                "%s{\"node_id\":%d,\"temp\":%.2f,\"hum\":%.2f,\"press\":%.2f,\"batt\":%.2f}",
                first ? "" : ",",
                i, nodes[i].sensor1, nodes[i].sensor2,
                nodes[i].sensor3, nodes[i].sensor4);
        }
        strncat(body, entry, sizeof(body) - strlen(body) - 1);
        first = false;
    }
    strncat(body, "]", sizeof(body) - strlen(body) - 1);
    if (!first)
        http_post_json("http://www.rxtx-designs.com/saa/upload_values.php", body);
}

// =========================================================================
// main()
// =========================================================================
int main(int argc, char** argv) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    log_msg("INFO", "Server starting up");
    if (!radio.begin()) {
        printf("CRITICAL: SPI Radio connection failed.\n");
        log_msg("ERROR", "Radio init failed — exiting");
        return 1;
    }

    radio.setPALevel(RF24_PA_HIGH);
    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.enableAckPayload();
    radio.enableDynamicPayloads();

    radio.stopListening();
    radio.closeReadingPipe(0);

    for (int i = 1; i <= 5; i++)
        radio.openReadingPipe(i, pipes[i]);

    updateConfigs();
    for (int i = 1; i <= 5; i++)
        radio.writeAckPayload(i, &sleep_cmds[i], sizeof(float));

    radio.startListening();
    last_upload = __millis();
    log_msg("INFO", "Radio OK — listening on pipes 1-5 (V1=20B V2=25B)");

    while (1) {
        updateConfigs();

        uint8_t pipeNum;
        if (radio.available(&pipeNum)) {
            uint8_t sz = radio.getDynamicPayloadSize();

            if (sz == sizeof(SensorPayload)) {
                // ---- Legacy HW 1.x node (20 bytes) ----
                SensorPayload incoming;
                radio.read(&incoming, sizeof(incoming));
                int id = incoming.nodeID;
                if (id >= 1 && id <= 5) {
                    nodes[id]        = incoming;
                    extras[id].is_v2 = false;
                    last_seen[id]    = time(NULL);
                    radio.writeAckPayload(pipeNum, &sleep_cmds[id], sizeof(float));
                    saveForUI();
                    log_msg("INFO", "RX NODE %d V1 | T:%.1f H:%.1f P:%.1f B:%.2f",
                            id, incoming.sensor1, incoming.sensor2,
                            incoming.sensor3, incoming.sensor4);
                } else {
                    log_msg("WARN", "RX V1 invalid nodeID=%d (pipe %u)", incoming.nodeID, pipeNum);
                }

            } else if (sz == sizeof(SensorPayloadV2)) {
                // ---- Sensor HW 2.0 node (25 bytes) ----
                SensorPayloadV2 incoming;
                radio.read(&incoming, sizeof(incoming));
                int id = incoming.nodeID;
                if (id >= 1 && id <= 5) {
                    nodes[id].nodeID  = incoming.nodeID;
                    nodes[id].sensor1 = incoming.sensor1;
                    nodes[id].sensor2 = incoming.sensor2;
                    nodes[id].sensor3 = incoming.sensor3;
                    nodes[id].sensor4 = incoming.sensor4;
                    extras[id].eco2   = incoming.eco2;
                    extras[id].tvoc   = incoming.tvoc;
                    extras[id].aqi    = incoming.aqi;
                    extras[id].is_v2  = true;
                    last_seen[id]     = time(NULL);
                    radio.writeAckPayload(pipeNum, &sleep_cmds[id], sizeof(float));
                    saveForUI();
                    log_msg("INFO", "RX NODE %d V2 | T:%.1f H:%.1f P:%.1f eCO2:%u TVOC:%u AQI:%u",
                            id, incoming.sensor1, incoming.sensor2, incoming.sensor3,
                            incoming.eco2, incoming.tvoc, incoming.aqi);
                } else {
                    log_msg("WARN", "RX V2 invalid nodeID=%d (pipe %u)", incoming.nodeID, pipeNum);
                }

            } else {
                // Unknown payload size — drain the buffer to avoid stalls
                uint8_t buf[32];
                radio.read(buf, sz < 32 ? sz : 32);
                log_msg("WARN", "Unknown payload size %u on pipe %u — discarded", sz, pipeNum);
            }
        }

        if (__millis() - last_printout > NODE_PRINTOUT_DELAY) {
            printNodes();
            last_printout = __millis();
        }

        if (__millis() - last_upload > NODE_UPLOAD_DELAY) {
            uploadData();
            last_upload = __millis();
        }

        usleep(1000);
    }
    return 0;
}
