# ESP32 wideband BLE bridge

Companion firmware for **R53 Logger - Flasher**. Advertises as `R53-Wideband`
and notifies a little-endian float of **sensor volts** (0–5 V). The phone maps
volts → AFR.

See comments at the top of `esp32_wideband_bridge.ino` for wiring, GATT UUIDs,
and ADS1115 vs resistor-divider modes.

**Library:** [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)
