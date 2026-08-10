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

| Project | Role |
|--------|------|
| [`esp32_wideband_shiftlight/`](./esp32_wideband_shiftlight/) | **One-box**: wideband bridge + WS2812B shift light, RPM written back by the app over the same BLE link |
| [`esp32_wideband_bridge/`](./esp32_wideband_bridge/) | PlatformIO BLE bridge: streams wideband **0–5 V** to the logger |
| [`esp32_kline_sniffer/`](./esp32_kline_sniffer/) | Arduino K-line listen-only sniffer (ISTA/INPA capture) |

Wideband bridge: `cd esp32_wideband_bridge && pio run -t upload`. See that folder’s [WIRING.md](./esp32_wideband_bridge/WIRING.md) and [wiring.svg](./esp32_wideband_bridge/wiring.svg).

## License

MIT — see [LICENSE](./LICENSE).
