# Arduino Pro Mini — Sensor Node HW 3.0
## Project Summary

---

## Goal
Battery-powered, wireless environmental sensor node on a custom PCB.
Reads temperature, humidity, pressure, eCO2, TVOC and air quality index,
then transmits data wirelessly via the NRF24L01+PA radio module.

---

## Hardware

| Part | Function | Interface | I2C Addr |
|---|---|---|---|
| Arduino Pro Mini (ATmega328P, 3.3 V / 8 MHz) | Main MCU | — | — |
| GY-BME280 | Temp / Humidity / Pressure | I2C | 0x76 |
| ENS160 breakout | eCO2 / TVOC / AQI | I2C | 0x52 |
| NRF24L01+PA (E01-ML01DPA_TH) | 2.4 GHz wireless TX | SPI | — |
| SW1 WS-DITV_A18117270905 | 5-position DIP — node ID + PA level | GPIO | — |
| MCP1700T-3302E_TT | 3.3 V LDO regulator | — | — |

Power supply: single-cell battery via J1 connector → MCP1700T → 3.3 V rail.

---

## Pin Assignments

### SPI — NRF24L01+PA
| Arduino pin | Signal | NRF24 pin |
|---|---|---|
| D9  | CE   | CE   |
| D10 | CSN  | CSN  |
| D11 | MOSI | MOSI |
| D12 | MISO | MISO |
| D13 | SCK  | SCK  |

### I2C — BME280 and ENS160 (shared bus)
| Arduino pin | Signal |
|---|---|
| A4 | SDA |
| A5 | SCL |

Pull-ups: 4.7 kΩ from SDA and SCL to 3.3 V (check breakout boards for onboard pull-ups).

### SW1 DIP switch (active-low, pull-up enabled in firmware; other side of all → GND)

| Switch | Arduino pin | Role | Bit weight |
|---|---|---|---|
| P1 | D8 | NODE_ID bit 2 | 4 |
| P2 | D7 | NODE_ID bit 1 | 2 |
| P3 | D6 | NODE_ID bit 0 | 1 |
| P4 | D5 | PA level bit 1 | 2 |
| P5 | D4 | PA level bit 0 | 1 |

#### NODE_ID table (P1–P3)
| NODE_ID | P1 | P2 | P3 |
|---|---|---|---|
| 1 | OFF | OFF | ON  |
| 2 | OFF | ON  | OFF |
| 3 | OFF | ON  | ON  |
| 4 | ON  | OFF | OFF |
| 5 | ON  | OFF | ON  |
| 6 | ON  | ON  | OFF |

#### PA level table (P4–P5)
| PA level | P4 | P5 |
|---|---|---|
| RF24_PA_MIN  | OFF | OFF |
| RF24_PA_LOW  | OFF | ON  |
| RF24_PA_HIGH | ON  | OFF |
| RF24_PA_MAX  | ON  | ON  |

### Mode / address straps (one-time jumpers)
| Pin | Connect to | Effect |
|---|---|---|
| BME280 CSB | 3.3 V | Force I2C mode |
| BME280 SDO | GND   | Set I2C addr → 0x76 |
| ENS160 CS  | 3.3 V | Force I2C mode |
| ENS160 ADD | GND   | Set I2C addr → 0x52 |

---

## Operating modes — auto-detected at boot by I2C scan

| Sensors detected | Mode | Sleep strategy | sensor4 |
|---|---|---|---|
| BME280 only | **BATTERY** | WDT `SLEEP_MODE_PWR_DOWN` | battery voltage (V) |
| BME280 + ENS160 | **USB** | `delay()` — MCU stays awake | 0.0 (USB powered) |

### BATTERY mode optimisations
- WDT 8-second ticks; transmit every `sleep_multiplier × 8 s`
- BOD (Brown-Out Detection) disabled before each sleep (~20 µA saving)
- ADC disabled during sleep
- BME280 configured in **forced mode** — single-shot measurement then back to ~0.1 µA sleep
- Radio `powerDown()` between transmissions

### USB mode
- `delay()` between transmissions keeps the MCU and I2C bus alive
- ENS160 maintains its internal gas-sensor baseline continuously
- BME280 in normal mode; temp/humidity fed to ENS160 each cycle for compensation
- `sensor4 = 0.0` (no battery circuit on USB nodes)
- ENS160 requires ~1 h warm-up — `aqi = 0` until baseline is stable

---

## Protocol

- **Payload**: 25-byte `SensorPayloadV2` (same as HW 2.0 — server identifies by dynamic payload size)
- **Radio**: 250 KBPS, CRC-16, auto-ACK, dynamic payloads
- **Address**: `0xABCDABCD00 + NODE_ID`
- **ACK payload**: server sends back `float` sleep multiplier

---

## Battery voltage

ATmega328P VCC measured against the internal 1.1 V bandgap reference.
Result in `sensor4` (battery mode only; 0.0 in USB mode).

---

## Libraries

```
adafruit/Adafruit BME280 Library
adafruit/Adafruit Unified Sensor
sciosense/ScioSense_ENS160
tmrh20/RF24
```

---

## Firmware

`firmware/Sensor_HW3_v1.ino`
