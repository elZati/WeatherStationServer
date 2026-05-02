# GEMINI Multi-Sensor Control Center: Technical Documentation (v19)

## 1. System Overview

The GEMINI Weather Station is a multi-tier IoT architecture. It collects environmental data from up to 5 remote Arduino nodes and visualises it on a Raspberry Pi 7-inch touchscreen (1024×600).

---

## 2. The Data Pipeline

| Stage | Component | Detail |
|-------|-----------|--------|
| Source | Arduino nodes | Sleep via WDT (8s cycles × multiplier). Wake, read HTU21D + optional BMP180, pack into 20-byte struct, transmit via nRF24L01. |
| Bridge | C++ server (`Server_v15.cpp`) | Receives packets, updates `live_data.json`, sends ACK sleep command back, uploads to remote DB every 5 min. |
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
- Show: temperature (large), humidity, battery voltage (red if < 2.6V), pressure + trend (nodes with BMP180), time since last TX.
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
- Multiplier = `max(1, round(minutes × 60 / 8))` (8-second WDT cycles).
- C++ server picks up `config.txt` every 2s and flushes/reloads ACK payloads.
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

The GUI keeps `previous_values[nid]` (last seen temperature). The `last_packet_time` only resets when the temperature value actually changes. This prevents the "TX: 0s ago" timer from resetting every 2-second poll cycle when the node is sleeping and the JSON hasn't changed.

---

## 6. Troubleshooting

| Symptom | Check |
|---------|-------|
| No cards appear | `cat live_data.json` — empty means C++ server isn't writing |
| Cards keep disappearing | Timeout too short; sleep multiplier × 3 < actual sleep interval |
| Pressure shows `----` | Node doesn't have BMP180, or is sending sentinel `33.33` |
| Forecast not updating | Check internet on Pi; location name valid for Open-Meteo geocoding API |
| Sleep command not reaching node | Check `config.txt` exists; verify ACK payload in serial monitor |
