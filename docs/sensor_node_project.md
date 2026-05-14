# ESP32-C3 SuperMini — Wireless Environmental Sensor Node
## Project Summary / Handoff Document

---

## Goal
Battery-powered, wireless environmental sensor node on a breadboard.
Reads temperature, humidity, pressure, eCO2, TVOC and air quality index,
then transmits data wirelessly via the NRF24L01+PA radio module.

---

## Hardware

| Board | Part | Interface | I2C Addr |
|---|---|---|---|
| ESP32-C3 SuperMini (HW-466AB) | Main MCU | — | — |
| GY-BME/BMP280 | Temp / Humidity / Pressure | I2C | 0x76 |
| ENS160 breakout | eCO2 / TVOC / AQI | I2C | 0x52 |
| NRF24L01+PA breakout | 2.4 GHz wireless TX | SPI | — |

Power supply: external bench supply, 3.3V and 5V rails available.
All active components run on 3.3V.

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
| 7 | GPIO20 | Free (UART RX) |
| 8 (bottom) | GPIO21 | Free (UART TX) |

### Right side pins (top → bottom, USB-C at top):
| Physical order | Pin | Project role |
|---|---|---|
| 1 (top) | 5V | Not used (VBUS passthru) |
| 2 | GND | Ground → GND rail |
| 3 | 3.3V | Power → 3.3V rail ✓ DONE |
| 4 | GPIO4 | I2C SDA |
| 5 | GPIO3 | Free |
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

// BME280
Adafruit_BME280 bme;
bme.begin(0x76);
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
- NRF24L01+PA antenna needs clear line of sight; keep away from metal objects
  and other 2.4 GHz sources (WiFi, Bluetooth) during initial range testing.
