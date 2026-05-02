# GEMINI Server Logic: Raspberry Pi C++ Monitor (v15)

This document provides a detailed technical breakdown of the `Server_RX_mode_GEMINI_v15_multi.cpp` file, which serves as the central receiving hub for the GEMINI sensor network.

## 1. Overview
The server is designed to run on a Raspberry Pi, utilizing an nRF24L01 radio module to communicate with up to five remote Arduino nodes. Its primary responsibilities include:
* **Radio Management:** Receiving sensor data and sending back timing commands.
* **Data Synchronization:** Ensuring data structures match between different hardware architectures.
* **System Integration:** Updating a local JSON for the GUI and uploading data to a remote MySQL database.

## 2. Key Architectural Fixes
This version (v13.3 logic within v15) includes critical stability improvements [cite: 1]:

* **`#pragma pack(1)` (Data Alignment):** Raspberry Pi (ARM) and Arduino (AVR) handle memory differently. By default, the Pi adds "padding" bytes to align data. This directive forces the Pi to use a "packed" 20-byte structure, ensuring that Node 3's data isn't misinterpreted as negative or garbled [cite: 1].
* **`radio.flush_tx()` (Buffer Management):** The nRF24L01 has a 3-slot hardware buffer for ACK payloads. Without flushing, new sleep commands can get stuck behind old ones. This code clears the queue whenever a GUI change is detected [cite: 1].
* **Pre-emptive Loading:** ACK payloads are primed before the radio starts listening, ensuring nodes receive instructions on their very first contact [cite: 1].

---

## 3. Data Structure
The `SensorPayload` struct is the backbone of the communication protocol [cite: 1]:

| Component | Type | Size | Description |
| :--- | :--- | :--- | :--- |
| `nodeID` | `int32_t` | 4 Bytes | Identifies which node (1-5) sent the data [cite: 1]. |
| `sensor1` | `float` | 4 Bytes | Ambient Temperature (HTU21D) [cite: 1]. |
| `sensor2` | `float` | 4 Bytes | Relative Humidity (HTU21D) [cite: 1]. |
| `sensor3` | `float` | 4 Bytes | Atmospheric Pressure (BMP180) [cite: 1]. |
| `sensor4` | `float` | 4 Bytes | Battery Voltage [cite: 1]. |

**Total Size:** 20 Bytes [cite: 1].

---

## 4. Core Functional Modules

### A. Radio Configuration (`main`)
The radio is configured for long-range, reliable communication [cite: 1]:
* **Data Rate:** `250KBPS` for maximum sensitivity [cite: 1].
* **CRC:** `16-bit` to ensure data integrity [cite: 1].
* **Pipes:** Opens five unique reading pipes (`0xABCDABCD01` through `05`) to track nodes individually [cite: 1].
* **AckPayload:** Enables the "Talk-back" feature, allowing the Pi to embed a sleep command inside the hardware acknowledgement [cite: 1].

### B. Configuration Monitoring (`updateConfigs`)
Every 2 seconds, the server checks `config.txt` for changes made by the Python GUI sliders [cite: 1]. If the sleep multipliers have changed:
1.  Listening is paused.
2.  The hardware TX buffer is flushed.
3.  New values are reloaded into all 5 pipe buffers [cite: 1].

### C. The Receiver Loop
The `while(1)` loop is the engine of the server [cite: 1]:
1.  **Poll Radio:** Checks if data is available on any pipe [cite: 1].
2.  **Read Payload:** Extracts the 20-byte packet into memory [cite: 1].
3.  **Reload ACK:** Immediately puts the latest sleep command back into that pipe's buffer for the node's *next* wake cycle [cite: 1].
4.  **Save/Export:** Triggers `saveForUI()` to update the `live_data.json` file used by the Python dashboard [cite: 1].

### D. External Integration
* **`printNodes()`:** Renders a real-time dashboard in the Linux terminal with timestamps and formatted sensor readings.
* **`http_get(url)`:** Internal helper that performs a fire-and-forget HTTP GET via `libcurl` with a 10-second timeout. Response body is discarded. `curl_global_init` is called once at program start in `main()`.
* **`uploadData()`:** Every 5 minutes, iterates over all active nodes (1–5) and uploads each one individually to `upload_values.php` with parameters `node_id`, `temp`, `hum`, `press`, `batt`. Nodes that have never been seen (`last_seen[i] == 0`) are skipped. This replaces the old approach that hardcoded Node 1 as outdoor and Node 2 as indoor.

---

## 5. Technical Requirements
* **Hardware:** Raspberry Pi with nRF24L01 (CE: 22, CSN: 0) [cite: 1].
* **Libraries:** `RF24` (Optimized for Pi), `libcurl`.
* **Compilation:** Requires linking with `-lrf24` and `-lcurl`.
