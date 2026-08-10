# R53 Wideband Bridge + Shift Light — one ESP32

Merges `esp32_wideband_bridge` and the standalone
[esp32-shift-light-R53-mini](https://github.com/MrBlahhhh/esp32-shift-light-R53-mini)
onto a single XIAO ESP32S3. The old setup needed two boards (wideband bridge on
BLE + shift light on ESP-NOW with its own RPM sender); here the phone is the
RPM source — the logger app already reads RPM off the K-line and writes it back
over the **same BLE connection** it uses for wideband volts.

```
ECU ── K-line ── phone app ── BLE ──> XIAO ──> 8x WS2812B shift light
wideband 0–5 V ── divider ──> XIAO ── BLE volts NOTIFY ──> phone app
```

The wideband half (UUIDs, payloads, divider, optional ADS1115) is byte-for-byte
compatible with `esp32_wideband_bridge` — an app that predates the shift light
still connects and streams volts; the LEDs just stay dark.

## Wiring

Everything from [`../esp32_wideband_bridge/WIRING.md`](../esp32_wideband_bridge/WIRING.md)
unchanged, plus the strip:

| Strip | XIAO |
|-------|------|
| 5V    | 5 V supply (NOT the 3V3 pin) |
| GND   | GND (common with XIAO + wideband) |
| DIN   | D1 (GPIO2) |

3.3 V data into a 5 V WS2812B strip usually works at 8 LEDs; add a 74AHCT125
level shifter if the first LED glitches. D0 stays the wideband ADC input,
D4/D5 stay I²C for the optional ADS1115.

## GATT additions

| Characteristic | UUID | Payload |
|---|---|---|
| RPM (WRITE / WRITE_NR) | `beb5483e-36e1-4688-b7f5-ea07361b26aa` | u16 LE rpm |
| Shift config (READ/WRITE) | `beb5483e-36e1-4688-b7f5-ea07361b26ab` | u16 LE green-start, u16 LE fade-start, u16 LE blink, u8 brightness (7 bytes) |

Defaults: dark < 3000, green to 6000, green→red fade to 7100, all-red 5 Hz
blink at 7100+, brightness 75/255. Config writes are all-or-nothing and must be
ordered (`green < fade < blink ≤ 9000`); they are RAM-only, so push them on
connect if you change them.

**Stale-RPM failsafe:** if no RPM write arrives for 1 s (app killed, link drop,
logging stopped) the strip goes dark instead of holding the last state, and a
BLE disconnect zeroes it immediately.

## Build

PlatformIO: `pio run -t upload` from this folder. NimBLE-Arduino 2.x and
FastLED 3.7+ pull in automatically (arduino-esp32 3.x core).
