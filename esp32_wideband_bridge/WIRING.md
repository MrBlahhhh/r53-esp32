# Wideband O2 Sensor Wiring — Seeed XIAO ESP32S3

The XIAO ESP32S3 reads the controller’s **analog 0–5 V AFR output** through a
**10 kΩ / 10 kΩ** divider so the ADC pin stays around **0–2.5 V**. See
[wiring.svg](wiring.svg) for a diagram.

Do **not** connect 5 V analog directly to any XIAO ADC pin (3.3 V max).

Board: [Seeed XIAO ESP32S3](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
(USB-C). Pin labels below are the silkscreen **D0 / D4 / D5** names.

## Common wire colors

### AEM wideband (your manual)

| Color | Function |
|-------|----------|
| **Red** | Switched 12V power |
| **Black** | Power ground |
| **White** (pin 9) | 0–5V analog **+** |
| **Brown** (pin 10) | 0–5V analog **−** → tie to GND |

### Innovate Motorsports LC-1 / LC-2

| Color | Function |
|-------|----------|
| **Red** | 12V power |
| **Blue/White** | Ground |
| **Yellow** | Analog 0–5V output |
| **Black** | LED / calibration wire |

> Wire colors can vary by harness revision. Always confirm against the controller’s manual for your exact unit.

## Connections to the XIAO ESP32S3

| Wideband signal | Connect to | Notes |
|-----------------|------------|-------|
| Analog 0–5V + (White / Yellow) | 10k/10k divider mid → **D0 (GPIO1 / A0)** | Midpoint ≈ 0–2.5 V |
| Analog − / Ground (Brown / Black) | **XIAO GND** + vehicle ground | Common ground required |
| 12V power (Red) | Vehicle switched +12V | Do **not** feed 12 V into the XIAO |
| XIAO power | USB-C **or** regulated **5V** pin | Not from vehicle 12 V |

### Recommended divider (both ADC methods)

```
Wideband 0–5 V out ──┬── 10k ── D0 (GPIO1)
                     └── 10k ── GND
```

Firmware undoes the divide with `DIVIDER_GAIN = 2.0` in `src/main.cpp`.

### Optional ADS1115 (3.3 V module)

| ADS1115 | Connect to |
|---------|------------|
| VDD | XIAO **3V3** |
| GND | XIAO **GND** |
| SDA | **D4 (GPIO5)** |
| SCL | **D5 (GPIO6)** |
| ADDR | GND (I²C address `0x48`) |
| A0 | Divider midpoint |

The phone selects resistor-ADC vs ADS1115 via the BLE hardware-mode characteristic.

## Build / flash

```bash
cd esp32_wideband_bridge
pio run -t upload
```

PlatformIO board id: `seeed_xiao_esp32s3`. Hold **B** (Boot) if the port
doesn’t appear, then tap **R** (Reset) while releasing Boot.
