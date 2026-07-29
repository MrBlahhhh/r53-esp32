# ESP32 wideband BLE bridge

Companion firmware for **R53 Logger - Flasher**. Advertises as `R53-Wideband`
and notifies a little-endian float of **sensor volts** (0–5 V). The phone maps
volts → AFR.

## Build (PlatformIO)

```bash
cd esp32_wideband_bridge
pio run -t upload
```

`platformio.ini` pulls in [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino). Source is in `src/main.cpp`.

## Wiring

- Diagram: [wiring.svg](wiring.svg)
- Wire colors & divider details: [WIRING.md](WIRING.md)

### Quick wire colors

**AEM:** Red = 12V power, Black = Ground, White = Analog 0–5V output, Brown = Sensor ground

**Innovate LC-1/LC-2:** Red = 12V power, Blue/White = Ground, Yellow = Analog 0–5V output, Black = LED/calibration wire

## GATT

| Characteristic | UUID | Role |
|----------------|------|------|
| Volts NOTIFY | `beb5483e-36e1-4688-b7f5-ea07361b26a8` | 4-byte LE float (0–5 V) |
| Hw mode R/W | `beb5483e-36e1-4688-b7f5-ea07361b26a9` | `0` = ESP32 ADC, `1` = ADS1115 |

Service: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
