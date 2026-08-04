# ESP32 wideband BLE bridge (XIAO ESP32S3)

Companion firmware for **R53 Logger - Flasher**. Runs on a
[Seeed XIAO ESP32S3](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/),
advertises as `R53-Wideband`, and notifies a little-endian float of **sensor
volts** (0–5 V). The phone maps volts → AFR.

## Build (PlatformIO)

```bash
cd esp32_wideband_bridge
pio run -t upload
```

Board: `seeed_xiao_esp32s3`. `platformio.ini` pulls in
[NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino). Source is in
`src/main.cpp`.

## Pins (XIAO silkscreen)

| Role | XIAO pin | GPIO |
|------|----------|------|
| Analog mid (divider) | **D0 / A0** | GPIO1 |
| ADS1115 SDA (optional) | **D4 / SDA** | GPIO5 |
| ADS1115 SCL (optional) | **D5 / SCL** | GPIO6 |
| Common ground | **GND** | — |
| Board power | USB-C or **5V** | — |

## Wiring

- Diagram: [wiring.svg](wiring.svg)
- Wire colors & divider details: [WIRING.md](WIRING.md)

### Quick wire colors

**AEM:** Red = 12V, Black = GND, White = Analog+, Brown = Analog− → GND

**Innovate LC-1/LC-2:** Red = 12V, Blue/White = Ground, Yellow = Analog 0–5V, Black = LED/cal

## GATT

| Characteristic | UUID | Role |
|----------------|------|------|
| Volts NOTIFY | `beb5483e-36e1-4688-b7f5-ea07361b26a8` | 4-byte LE float (0–5 V) |
| Hw mode R/W | `beb5483e-36e1-4688-b7f5-ea07361b26a9` | `0` = XIAO ADC, `1` = ADS1115 |

Service: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
