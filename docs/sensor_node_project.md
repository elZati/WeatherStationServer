# ESP32-C3 SuperMini — Wireless Environmental Sensor Node (HW 2.0)
## Project Summary / Handoff Document

---

## Goal
Wireless environmental sensor node on a custom PCB.
Reads temperature, humidity, pressure, and optionally eCO2, TVOC and AQI,
then transmits data wirelessly via the NRF24L01+PA radio module.

---

## Operating modes — auto-detected at boot by I2C scan

| Sensors detected | Mode | Sleep strategy | sensor4 |
|---|---|---|---|
| BME280 only | **BATTERY** | ESP32 deep sleep (full reboot on wake) | 0.0 (no ADC circuit) |
| BME280 + ENS160 | **USB** | `delay()` — MCU stays awake | 0.0 (USB powered) |

### BATTERY mode
- After each transmit cycle, calls `esp_deep_sleep_start()` — ESP32 fully powers down
- On wake the MCU reboots into `setup()` (loop() is never used in this mode)
- `sleep_multiplier` stored in `RTC_DATA_ATTR` so it survives deep sleep resets
- Typical ESP32-C3 deep-sleep current: ~5 µA

### USB mode
- `delay()` between transmissions keeps MCU and I2C bus alive
- ENS160 maintains its internal gas-sensor baseline continuously
- ENS160 requires ~1 h warm-up — `aqi = 0` until baseline is stable
- `sensor4 = 0.0` (USB powered, no battery circuit in schematic)

---

## Hardware

| Board | Part | Interface | I2C Addr |
|---|---|---|---|
| ESP32-C3 SuperMini (HW-466AB) | Main MCU | — | — |
| GY-BME/BMP280 | Temp / Humidity / Pressure | I2C | 0x76 |
| ENS160 breakout | eCO2 / TVOC / AQI (USB nodes only) | I2C | 0x52 |
| NRF24L01+PA breakout | 2.4 GHz wireless TX | SPI | — |

Power supply: battery via J1 + MCP1700T-3302E LDO (battery nodes) or USB-C (USB nodes).
All active components run on 3.3 V.

---

## ESP32-C3 SuperMini (HW-466AB) Pinout
Board model confirmed from physical silkscreen photo.
USB-C faces left when seated on breadboard.

### Left side pins (top → bottom, USB-C at top):
| Physical order | GPIO | Project role |
|---|---|---|
| 1 (top) | GPIO5 | I2C SCL |
| 2 | GPIO6 | SPI MISO |
| 3 | GPIO7 | SPI MOSI |
| 4 | GPIO8 | ⚠ Onboard LED — do not use |
| 5 | GPIO9 | ⚠ BOOT button — do not use |
| 6 | GPIO10 | SPI SCK |
| 7 | GPIO20 | SW1 P1 — node ID bit 2 (value 4) |
| 8 (bottom) | GPIO21 | SW1 P2 — node ID bit 1 (value 2) |

### Right side pins (top → bottom, USB-C at top):
| Physical order | Pin | Project role |
|---|---|---|
| 1 (top) | 5V | Not used (VBUS passthru) |
| 2 | GND | Ground → GND rail |
| 3 | 3.3V | Power → 3.3V rail ✓ DONE |
| 4 | GPIO4 | I2C SDA |
| 5 | GPIO3 | SW1 P3 — node ID bit 0 (value 1) |
| 6 | GPIO2 | ⚠ Strapping pin — avoid |
| 7 | GPIO1 | NRF24 CSN |
| 8 (bottom) | GPIO0 | NRF24 CE |

---

## Wiring: Complete Pin Connections

### Power
| From | To | Wire colour |
|---|---|---|
| ESP 3.3V pin (right col, pin 3) | 3.3V rail (+) | Red |
| ESP GND pin (right col, pin 2) | GND rail (−) | Black |
| BME280 VCC | 3.3V rail | Red |
| BME280 GND | GND rail | Black |
| ENS160 VCC | 3.3V rail | Red |
| ENS160 GND | GND rail | Black |
| NRF24 VCC | 3.3V rail | Red |
| NRF24 GND | GND rail | Black |

### I2C bus (shared by BME280 and ENS160)
| From (ESP) | Signal | BME280 pin | ENS160 pin |
|---|---|---|---|
| GPIO5 | SCL | SCL | SCL |
| GPIO4 | SDA | SDA | SDA |

Pull-up resistors: 4.7 kΩ from SDA to 3.3V, 4.7 kΩ from SCL to 3.3V.
Check if your breakouts already have onboard pull-ups before adding external ones.

### SPI bus (NRF24L01+PA only)
| ESP GPIO | Signal | NRF24 pin |
|---|---|---|
| GPIO7 | MOSI | MOSI |
| GPIO6 | MISO | MISO |
| GPIO10 | SCK | SCK |
| GPIO0 | CE | CE |
| GPIO1 | CSN | CSN |

### SW1 — DIP switch node ID selector
SW1 (WS-DITV_A18117270905) sets NODE_ID at boot (active-low, firmware enables pull-ups).
Other side of all three switches connects to GND.

| Switch | GPIO | Bit | Value | Closed = adds |
|---|---|---|---|---|
| P1 | GPIO20 | bit 2 | 4 | yes |
| P2 | GPIO21 | bit 1 | 2 | yes |
| P3 | GPIO3  | bit 0 | 1 | yes |

NODE_ID = sum of closed switch values. Valid range: 1–5.

| NODE_ID | P1 | P2 | P3 |
|---|---|---|---|
| 1 | OFF | OFF | ON  |
| 2 | OFF | ON  | OFF |
| 3 | OFF | ON  | ON  |
| 4 | ON  | OFF | OFF |
| 5 | ON  | OFF | ON  |

### Mode / address strap wires (short jumpers, set once)
| Pin | Connect to | Purpose |
|---|---|---|
| BME280 CSB | 3.3V | Selects I2C mode (not SPI) |
| BME280 SDO | GND | Sets I2C address to 0x76 |
| ENS160 CS | 3.3V | Selects I2C mode (not SPI) |
| ENS160 ADD | GND | Sets I2C address to 0x52 |

### NRF24L01+PA decoupling (important!)
Place physically across the NRF24 VCC and GND pins on the breadboard:
- 100 µF electrolytic capacitor
- 100 nF ceramic capacitor
The PA variant draws up to ~115 mA peak; without caps it will brown-out and behave erratically.

---

## BME280 breakout (GY-BME/BMP280) pin order
Single row, left → right as seen from component side:
`VCC · GND · SCL · SDA · CSB · SDO`

## ENS160 breakout pin order
Single row, left → right as seen from component side:
`INT · ADD · CS · SDA · SCL · GND · VCC`
- INT: leave unconnected (not needed for polling mode)

## NRF24L01+PA pin order
2×4 grid, standard pinout:
```
Top row (left→right): GND  VCC  CE   CSN
Bot row (left→right): SCK  MOSI MISO IRQ
```
- IRQ: leave unconnected

---

## Build progress
- [x] Step 1: ESP32-C3 SuperMini placed on breadboard, 3V3 and GND wired to rails
- [x] Step 2: BME280 — add VCC, GND, SCL, SDA, CSB strap, SDO strap
- [x] Step 3: ENS160 — add VCC, GND, SCL, SDA, CS strap, ADD strap
- [x] Step 4: NRF24L01+PA — add VCC, GND, CE, CSN, SCK, MOSI, MISO + decoupling caps

## Assembly order — IMPORTANT

**SW1 DIP switch physically blocks the USB-C port once soldered.**
Follow this order for every new board:

1. Assemble board **without** SW1
2. Flash firmware via USB-C with `NODE_ID` hardcoded (DIP reading disabled)
3. Confirm WiFi connects and OTA/Telnet are reachable
4. Solder SW1, set switches for desired NODE_ID
5. Flash production firmware (DIP reading enabled) **via OTA** — `pio run -e ota -t upload`

> ⚠ PCB revision needed: reposition SW1 so it does not block USB-C.

---

## Recommended libraries (Arduino / PlatformIO)

```ini
; platformio.ini
[env:esp32-c3-devkitm-1]
platform  = espressif32
board     = esp32-c3-devkitm-1
framework = arduino

lib_deps =
    adafruit/Adafruit BME280 Library
    adafruit/Adafruit Unified Sensor
    tmrh20/RF24
    ; ENS160: install manually from https://github.com/sciosense/ENS160_driver
    ; or use: dfrobot/DFRobot_ENS160
```

### Key library calls

```cpp
#include <Wire.h>               // I2C — built-in
#include <SPI.h>                // SPI — built-in
#include <Adafruit_Sensor.h>    // Unified sensor base
#include <Adafruit_BME280.h>    // BME280
#include <ScioSense_ENS160.h>   // ENS160
#include <RF24.h>               // NRF24L01+PA

// I2C pins (must call before Wire.begin())
#define I2C_SDA 4
#define I2C_SCL 5
Wire.begin(I2C_SDA, I2C_SCL);

// BME280 — forced mode (sensor sleeps between cycles, wakes on demand)
Adafruit_BME280 bme;
bme.begin(0x76);
bme.setSampling(Adafruit_BME280::MODE_FORCED,
                Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
                Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::FILTER_OFF,
                Adafruit_BME280::STANDBY_MS_0_5);
bme.takeForcedMeasurement();          // trigger before each read; sensor sleeps after
float temp     = bme.readTemperature();        // °C
float humidity = bme.readHumidity();           // %RH
float pressure = bme.readPressure() / 100.0F;  // hPa

// ENS160 — feed BME280 compensation data each cycle
ens160.set_envdata(temp, humidity);
uint16_t eco2 = ens160.geteCO2();  // ppm
uint16_t tvoc = ens160.getTVOC();  // ppb
uint8_t  aqi  = ens160.getAQI();   // 1–5

// NRF24
RF24 radio(0, 1);  // CE=GPIO0, CSN=GPIO1
radio.begin();
radio.setPALevel(RF24_PA_LOW);    // start low, increase if range is poor
radio.setDataRate(RF24_250KBPS);  // 250 kbps = better range + lower power
radio.openWritingPipe(address);
radio.write(&payload, sizeof(payload));
```

---

## WiFi / OTA / Telnet (USB mode only)

USB-mode nodes (BME280 + ENS160) run WiFi at boot for wireless firmware updates and remote monitoring. Battery-mode nodes skip WiFi entirely.

| Feature | Detail |
|---|---|
| OTA hostname | `hw20-node4.local` |
| OTA password | `kuningas` |
| OTA upload command | `pio run -e ota -t upload` |
| Telnet monitor | `telnet <ip> 23` |
| IP shown at boot | Serial + first Telnet connection |

The `[env:ota]` section in `platformio.ini` points to the board IP. Update it if DHCP reassigns the address, or reserve `ac:a7:04:c0:c2:b4` in the router for a stable IP.

---

## Diagnostic firmware

`firmware/Sensor_HW2_debug/` — standalone diagnostic sketch for hardware bring-up and fault isolation.

Tests run each cycle (8 s):
- **I2C scan** — lists all detected devices
- **BME280** — init + range-validated readings
- **ENS160** — init + AQI/eCO2/TVOC readings
- **NRF24** — SPI, isChipConnected(), TX attempt with ARC count + register dump

Additional one-shot tests available in the source (not in default loop):
- `testCEPin()` — toggles GPIO0 every 5 s for multimeter verification
- `testNRF24_RX()` — listens on all 5 node pipes for 60 s to verify RX path

Flash with: `pio run -t upload` from `firmware/Sensor_HW2_debug/`

---

## Notes
- Development environment: VS Code on Windows 11, SSH to Raspberry Pi 400,
  PlatformIO recommended over Arduino IDE for dependency management.
- ENS160 requires several minutes warm-up before readings stabilise.
- ENS160 accuracy improves significantly when fed live BME280 temp/humidity
  compensation values each measurement cycle.
- GPIO8 and GPIO9 on the SuperMini are permanently connected to the onboard
  LED and BOOT button respectively — never wire peripherals to these.
- GPIO2 is a strapping pin; leaving it floating is fine, but don't pull it
  low at boot.
- NRF24 TX failure with ARC=15 and working RX indicates a defective module (TX output stage dead). Replace the module.
- High-pitched noise from the board is coil whine from ceramic caps under ENS160 heater load — harmless.
