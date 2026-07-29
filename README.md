# R53 ESP32 firmware & app privacy

Public companion to the MINI R53 (W11) Android tools:

- **R53 Logger - Flasher** (`com.geekopolis.r53logger`)
- **R53 Coding** (`com.geekopolis.r53code`)

This repo holds the open ESP32 sketches and the **Play Store privacy policy**.

## Privacy policy (Play Console URL)

**Working now (no Pages setup):**

`https://cdn.jsdelivr.net/gh/MrBlahhhh/r53-esp32@main/privacy.html`

**After GitHub Pages is enabled** (Settings → Pages, or the Actions workflow in this repo):

`https://mrblahhhh.github.io/r53-esp32/privacy.html`


## Firmware

| Sketch | Role |
|--------|------|
| [`esp32_wideband_bridge/`](./esp32_wideband_bridge/) | BLE bridge: streams wideband **0–5 V** to the logger |
| [`esp32_kline_sniffer/`](./esp32_kline_sniffer/) | Passive K-line listen-only sniffer (ISTA/INPA capture) |

Flash with Arduino IDE + Espressif ESP32 board support. See each folder for wiring.

## License

MIT — see [LICENSE](./LICENSE).
