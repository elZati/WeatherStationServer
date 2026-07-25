# GEMINI Multi-Sensor Control Center: Technical Documentation (v19)

## 1. System Overview

The GEMINI Weather Station is a multi-tier IoT architecture. It collects environmental data from up to 5 remote Arduino nodes and visualises it on a Raspberry Pi 7-inch touchscreen (1024×600).

---

## 2. The Data Pipeline

| Stage | Component | Detail |
|-------|-----------|--------|
| Source (HW 1.x) | ATmega Pro Mini nodes | Sleep via WDT (8s × multiplier). Wake, read HTU21D + optional BMP180, transmit **20-byte** struct via nRF24L01. |
| Source (HW 2.0) | ESP32-C3 SuperMini nodes | Deep-sleep (timer-wakeup). Wake, read BME280 + ENS160, transmit **25-byte** struct via nRF24L01. |
| Source (HW 3.0) | ATmega Pro Mini nodes | Sleep via WDT (8s × multiplier). Wake, read BME280, transmit **25-byte** struct via nRF24L01+PA. Battery voltage via bandgap. ENS160 not populated. |
| Bridge | C++ server (`Server_v15.cpp`) | Detects payload size (20 vs 25 bytes). Updates `live_data.json`, sends ACK sleep command, uploads to remote DB every 15 min. |
| Interface | Python GUI (`gui/gui_v19.py`) | Reads `live_data.json` every 2s, renders sensor cards and live graphs. |
| Web | PHP + Chart.js (`saa/`) | Reads remote MySQL DB (`node_readings` table), shows historical charts and per-node cards. |

---

## 3. GUI Layout

### Normal mode (sidebar visible)
```
[Node 1 card] [Node 2 card] … [Forecast card] [▶ toggle]
[Temperature chart                                      ]
[Humidity chart                                         ]
[SYSTEM SETTINGS sidebar (right column)                 ]
```

### Fullscreen mode (sidebar hidden, toggle button pressed)
```
[◀ btn          ] ← right panel (240 px wide)
[3°/12°C · desc ]
[Node 1 card    ]
[Node 2 card    ]
[Temperature + Humidity charts fill full height on left ]
```

The layout switches by moving `self.header` between grid positions and reconfiguring the canvas rowspan. `_repack_header("vertical"|"horizontal")` rebuilds the pack order from scratch on every toggle to avoid drift.

---

## 4. Key GUI Features

### A. Sensor Cards
- Dynamically created when a node first checks in.
- All nodes show: temperature (large), humidity, battery voltage (red if < 2.6V), pressure + trend, TX age.
- ENS160 fields (AQI, eCO2, TVOC) only appear if the node sends non-zero `eco2` or `aqi` values — nodes without ENS160 (HW 3.0, test boards) get a clean card without those rows. Same rule applies to the web page (`graph.html`).
- Battery shows "USB Pwr" when `batt=0`.
- Click a card to toggle that node's line on/off in the graph.
- Cards auto-hide after timeout (longest sleep setting × 3, minimum 60s) and reappear when data resumes.
- Font sizes switch between `CARD_FONTS_NORMAL` and `CARD_FONTS_FULL` on fullscreen toggle.

### B. Pressure Trend
- Computed from the pressure history buffer over the selected time window.
- Normalised to hPa per 3 hours (meteorological standard).
- `▲` rising (> +1.5 hPa/3h), `▼` falling (< −1.5 hPa/3h), `─` stable.
- Nodes sending the sentinel value `33.33` (no BMP180) show no pressure row.

### C. Tomorrow's Forecast Card
- Fetches from Open-Meteo: `daily` (temp max/min, weather code, wind) + `hourly=cape` for `forecast_days=2`.
- Also shows lightning potential: next-6-hour max CAPE from `hourly.cape`, displayed as ⚡ Low / ⚡⚡ Moderate / ⚡⚡⚡ High with colour (yellow → orange → red). No label shown when CAPE < 100 J/kg.
- Refreshes every 6 hours in a background thread.
- In fullscreen mode replaced by a compact single-line strip: `"3° / 12°C · Partly cloudy"` (`wx_compact_lbl`).
- Location configurable via sidebar entry field; defaults to `Tampere`.

### D. Sleep Control
- Sleep steps (minutes): `[0, 1, 2, 3, 4, 5, 10, 15, 20, 30, 60]`. Step 0 = "Fastest".
- Per-node sliders in sidebar; "Update Arduino Timing" writes multipliers to `config.txt`.
- Multiplier calculated per node using the correct WDT cycle:
  - HW3 (Arduino, `batt > 0`): `max(1, round(minutes × 60 / 8))` — 8-second WDT cycles
  - HW2 (ESP32, `batt == 0`): `max(1, round(minutes × 60 / 12))` — 12-second deep-sleep cycles
- C++ server picks up `config.txt` every 2s; the updated multiplier is sent via ACK payload on the node's next TX.
- Node timeout auto-calculated: `max(60, max_sleep_min × 3 × 60)` seconds.

### E. Graph Windows
- `15min`: raw deque (up to 450 points).
- `1h`, `12h`, `24h`: time-bucketed averages (60s, 600s, 1200s buckets).
- Y-axis auto-scales to the visible data range with 15% padding.
- Nodes toggled off or timed out are excluded from scaling.

### F. Node Renaming
- Rename button per node in the sidebar opens a `CTkInputDialog`.
- Name stored in `self.sensor_names` (runtime only; not persisted to disk).
- Default names: `{1: "Outdoor", 2: "Indoor", 3: "Garden", 4: "Garage", 5: "Attic"}`.

---

## 5. Change Detection (TX Timer)

`last_packet_time[nid]` is set directly from the `last_seen` Unix timestamp written by the C++ server into `live_data.json`. This correctly tracks the server-side receive time regardless of whether sensor values changed between packets — important for nodes sending fixed or slowly-changing values.

`previous_values[nid]` (last seen temperature) is retained for other purposes but no longer gates the TX timer update.

If `live_data.json` is empty or malformed (race with server write), the GUI skips that poll cycle and reschedules normally — previous node state is preserved.

---

## 6. Troubleshooting

| Symptom | Check |
|---------|-------|
| No cards appear | `cat live_data.json` — empty means C++ server isn't writing |
| Cards keep disappearing | Timeout too short; sleep multiplier × 3 < actual sleep interval |
| Pressure shows `----` | Node doesn't have BMP180, or is sending sentinel `33.33` |
| AQI shows "—" (warming up) | ENS160 normal for first ~1h after power-on; values stabilise over time |
| Forecast not updating | Check internet on Pi; location name valid for Open-Meteo geocoding API |
| Sleep command not reaching node | Check `config.txt` exists; verify ACK payload in serial monitor. Note: NRF24 TX FIFO has only 3 slots — with 5 nodes, pre-loading ACK payloads for all pipes at startup would overflow the FIFO and silently drop pipes 4 and 5. The server intentionally starts with an empty FIFO and relies on the per-RX `writeAckPayload` call instead. After a server restart, each node misses its first ACK; the correct multiplier is applied from the second transmission onward. |
| Slow node (HW3) ignores sleep config — stays at 120–145s despite config set to fastest | Two causes. (1) **Node-side**: register analysis (HW3 v1.4 direct SPI reads) shows the E01-ML01DPA_TH clone actually **preserves** RF_SETUP (PA level, data rate), SETUP_RETR (retries), FEATURE (EN_DPL, EN_ACK_PAY), and DYNPD across powerDown/powerUp cycles. The original fix of re-calling `enableDynamicPayloads()` + `enableAckPayload()` after every `powerUp()` is kept for safety but may be acting via some other mechanism. **Critical**: do NOT re-call `setPALevel`, `setDataRate`, or `setRetries` in the TX cycle — on the E01-ML01DPA_TH these writes corrupt the radio state before TX and prevent ACK payloads from being received (HW3 v1.2 regression; fixed in v1.3 by removing them). Keep those calls in `setup()` only. (2) **Server-side**: with 4+ active nodes the 3-slot NRF24 TX FIFO is always full, so slow nodes' `writeAckPayload` calls are silently dropped. Fix (server v15.1+): `writeAckGuaranteed()` flushes FIFO and reloads the slow pipe when its TX gap exceeds 14s. |
| HW3 sleep intervals are ~18% longer than expected | The ATmega328P WDT RC oscillator runs slower than nominal at 3.3V/room temperature — measured at ~9.4s per 8s WDT cycle on tested units. Actual intervals: mult=1 → ~10s, mult=8 → ~75s (vs expected 66s), mult=38 → ~368s (vs expected 308s). This is within the ±20% WDT tolerance specified in the datasheet. ESP32 nodes (HW2) use a timer-based deep sleep and are much more precise (e.g. 8×12s = exactly 96s). No fix needed; factor in ~18% drift when setting sleep intervals. |
| ESP node stuck at default 120s interval despite server running | NRF24 clone register state can drift after weeks of uptime. `powerDown()`/`powerUp()` preserves registers per spec, but some clones accumulate bad state that only a full power removal (VCC=0) resets. Fix: power-cycle the ESP. The HW2 firmware (v1.2+) re-asserts `enableDynamicPayloads()` + `enableAckPayload()` before every TX and leaves the radio powered in USB mode to prevent recurrence. |
