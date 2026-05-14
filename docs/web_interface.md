# Web Interface Documentation

Files live in `saa/` (deployed to `public_html/saa/` on the cPanel hosting).

## Architecture

```
Browser → graph.html + weather.css
              ↓ fetch
         datafetcher.php  ←→  MySQL: node_readings table
         upload_values.php ←  Pi server (uploadData() every 5 min)
```

## Database

Table: `node_readings` (created by running `migrate.sql` in phpMyAdmin).

| Column | Type | Description |
|--------|------|-------------|
| `id` | INT AUTO_INCREMENT | Primary key |
| `node_id` | TINYINT UNSIGNED | Sensor node (1–5) |
| `temp` | FLOAT | Temperature °C |
| `hum` | FLOAT | Humidity %RH |
| `press` | FLOAT | Pressure hPa (33.33 = no sensor) |
| `batt` | FLOAT | Battery voltage V (0 = USB powered) |
| `eco2` | SMALLINT UNSIGNED NULL | eCO2 ppm — HW 2.0 nodes only |
| `tvoc` | SMALLINT UNSIGNED NULL | TVOC ppb — HW 2.0 nodes only |
| `aqi` | TINYINT UNSIGNED NULL | AQI 1–5 — HW 2.0 nodes only |
| `timestamp` | DATETIME | Auto-set on insert |

**Migration for HW 2.0 support** — run once in phpMyAdmin:
```sql
ALTER TABLE node_readings
    ADD COLUMN eco2 SMALLINT UNSIGNED DEFAULT NULL,
    ADD COLUMN tvoc SMALLINT UNSIGNED DEFAULT NULL,
    ADD COLUMN aqi  TINYINT UNSIGNED DEFAULT NULL;
```

## API — datafetcher.php

All responses are JSON. `Content-Type: application/json`.

| Action | Parameters | Returns |
|--------|-----------|---------|
| `action=latest` | — | Array of latest row per active node; HW 2.0 nodes include `eco2`, `tvoc`, `aqi` |
| `action=series` | `from`, `to` (YYYY-MM-DD) | Object keyed by node_id; array of `{ts, temp, hum, press, batt[, eco2, tvoc, aqi]}` |
| `action=stats` | `from`, `to` | Object keyed by node_id; `{min_temp, max_temp, …[, min_eco2, max_eco2]}` |

## Data Upload — upload_values.php

Called by Pi server `uploadData()` via **HTTP POST** with a JSON body every 15 minutes.

Body: JSON array of node objects — `[{"node_id":1,"temp":21.5,"hum":65.0,"press":1013.2,"batt":3.2}, …]`

Invalid or out-of-range `node_id` values are silently skipped. Returns `OK` on success, HTTP 400 if the body is not a valid JSON array.

## Web Page Features (graph.html)

- **Node cards**: one per active node; show temp/humidity/pressure/battery. HW 2.0 nodes also show AQI level (colour-coded 1–5) and eCO2/TVOC values. Battery shows "USB Pwr" when `batt=0`. Click to toggle chart line.
- **Tomorrow's forecast**: Open-Meteo daily API, same as local GUI.
- **Date range**: Today / 7 days / 30 days / custom date picker.
- **Charts**: Chart.js 4 with `chartjs-adapter-date-fns`; temperature, humidity, and (when HW 2.0 data is present) eCO2.
- **Stats table**: min/max temp and humidity per node for the selected range.
- **Node renaming**: Settings panel (⚙ button); names stored in `localStorage`.
- **Forecast location**: Configurable in settings; stored in `localStorage`.
- **Auto-refresh**: Latest cards every 60s; full data every 5 min when viewing Today.
- **Lightning potential (CAPE)**: Open-Meteo `hourly=cape` fetched alongside the forecast. Next-6-hour max CAPE shown in the forecast card as ⚡ Low / ⚡⚡ Moderate / ⚡⚡⚡ High with colour coding.
- **Real-time strikes (Blitzortung)**: WebSocket connection to `ws1/3/7.blitzortung.org`. Subscribes to a ±15° bounding box around the forecast location. Strikes within the configurable alert radius (default 100 km) appear in a pulsing orange banner showing count, closest distance, and compass bearing. Radius configurable in Settings panel and stored in `localStorage`.

## PWA (Progressive Web App)

The site is installable as a home screen app on iPhone via Safari:

1. Open Safari → navigate to the page
2. Tap the **Share** button → **Add to Home Screen**
3. A first-time banner in the page guides iOS users through this step

Key files:
- `manifest.json` — app name, theme color, icons
- `sw.js` — service worker: pre-caches static assets; API calls use network-first with offline fallback
- `icon.php` — PHP GD generates the PNG app icon at any requested size (`?size=192`)

The app works offline with the last-fetched sensor data shown until connectivity returns.

## Deployment

1. **First time (or adding HW 2.0 support)**: Run the ALTER TABLE migration above in cPanel → phpMyAdmin → SQL tab.
2. Upload `graph.html`, `weather.css`, `datafetcher.php`, `upload_values.php` to `public_html/saa/`.
3. Recompile and restart Pi server (`cd server && make && sudo ./Server`).
