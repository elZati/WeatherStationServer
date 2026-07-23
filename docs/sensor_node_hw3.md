# Arduino Pro Mini — Sensor Node HW 3.0
## Project Summary

---

## Goal
Battery-powered, wireless environmental sensor node on a custom PCB.
Reads temperature, humidity, and pressure, then transmits data wirelessly via the NRF24L01+PA radio module. ENS160 footprint present on PCB but not populated in current production builds.

---

## Hardware

| Part | Function | Interface | I2C Addr |
|---|---|---|---|
| Arduino Pro Mini (ATmega328P, 3.3 V / 8 MHz) | Main MCU | — | — |
| GY-BME280 | Temp / Humidity / Pressure | I2C | 0x76 |
| NRF24L01+PA (E01-ML01DPA_TH) | 2.4 GHz wireless TX | SPI | — |
| SW1 WS-DITV_A18117270905 | 5-position DIP — node ID | GPIO | — |
| MCP1700T-3302E_TT | 3.3 V LDO regulator | — | — |

Power supply: single-cell battery via J1 connector → MCP1700T → 3.3 V rail.

**Note:** Flash firmware before soldering SW1. SW1 blocks the USB-C port; USB programming is not possible once SW1 is installed.

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

### I2C — BME280 (shared bus, ENS160 footprint reserved)
| Arduino pin | Signal |
|---|---|
| A4 | SDA |
| A5 | SCL |

Pull-ups: 4.7 kΩ from SDA and SCL to 3.3 V (check breakout boards for onboard pull-ups).

### SW1 DIP switch — one-hot, active-low (all switches → GND on common side)

Exactly one switch ON at a time. The firmware reads the first LOW pin and assigns that NODE_ID.

| Switch | Arduino pin | NODE_ID assigned |
|---|---|---|
| P1 | D8 | 1 |
| P2 | D7 | 2 |
| P3 | D6 | 3 |
| P4 | D5 | 4 |
| P5 | D4 | 5 |

`HARDCODED_NODE_ID` in firmware overrides the DIP switch when set to 1–5. Set to 0 to use the switch.

---

## Operating Mode

HW 3.0 always runs in **BATTERY mode** (BME280 only):

- WDT 8-second ticks; transmit every `sleep_multiplier × 8 s` (default 15 = 120 s)
- BOD disabled before each sleep
- ADC disabled during sleep
- BME280 in **forced mode** — single-shot measurement then back to sleep
- Radio `powerDown()` between transmissions
- Battery voltage measured via ATmega internal 1.1 V bandgap — no resistor divider needed

Sleep multiplier updated by server ACK payload (float cast to uint8_t, range 1–200).

---

## Protocol

- **Payload**: 25-byte `SensorPayloadV2` — server identifies by dynamic payload size (20 B = HW1.x, 25 B = HW2/HW3)
- **Radio**: 250 KBPS, CRC-16, auto-ACK, dynamic payloads, `setRetries(15, 15)`
- **Channel**: 76
- **PA level**: `RF24_PA_HIGH`
- **Address**: `0xABCDABCD00 + NODE_ID`
- **ACK payload**: server sends back `float` sleep multiplier

### SensorPayloadV2 (25 bytes)
| Field | Type | Content |
|---|---|---|
| nodeID | int32_t | NODE_ID (1–5) |
| sensor1 | float | Temperature °C |
| sensor2 | float | Humidity %RH |
| sensor3 | float | Pressure hPa |
| sensor4 | float | Battery voltage V (LDO output) |
| eco2 | uint16_t | 0 (no ENS160) |
| tvoc | uint16_t | 0 (no ENS160) |
| aqi | uint8_t | 0 (no ENS160) |

---

## Battery Voltage

ATmega328P VCC measured against the internal 1.1 V bandgap reference — no external resistor divider required. Reports ~3.3 V when healthy (LDO in regulation); sags when battery depletes past LDO dropout (~2.0 V).

Calibration constant `BANDGAP_CAL = 1125300UL`. Adjust if reading differs from multimeter: `constant = measured_mV × ADC_result`.

---

## NRF24 Notes

- Module: E01-ML01DPA_TH (PA+LNA). Draws ~130 mA at PA_MAX.
- Power the RF module from battery, not FTDI 3.3 V — the FTDI supply (~50 mA) cannot sustain TX bursts and causes BOD brownout resets.
- A damaged PA chip shows ARC=15 (15 retransmits, no ACK). LNA (receive path) typically survives 5 V exposure; PA (transmit path) does not.
- Add a 100 µF decoupling cap across VCC/GND if using a breadboard test setup.

---

## Libraries

```
adafruit/Adafruit BME280 Library
adafruit/Adafruit Unified Sensor
tmrh20/RF24
rocketscream/Low-Power
```

---

## Firmware

Production: `firmware/Sensor_HW3/src/Sensor_HW3_v1.ino`
Debug/bring-up: `firmware/Sensor_HW3_debug/src/Sensor_HW3_debug.ino`

---

## NRF24 UNO Test Board

`firmware/NRF24_UNO_test/` — Arduino Uno (5 V / 16 MHz) test node for RF link diagnostics.

Hardcoded NODE_ID (set in firmware). Sends fixed test values (T=25, H=50, P=1013, V=5) every 10 s. No sleep — USB powered. Useful for verifying the server receive path and RF link without a production Pro Mini node.

Wiring (standard SPI):
| NRF24 pin | Uno pin |
|---|---|
| VCC | 3.3V |
| GND | GND |
| CE | D9 |
| CSN | D10 |
| SCK | D13 |
| MOSI | D11 |
| MISO | D12 |
