# Wideband O2 Sensor Wiring

The ESP32 reads the controller’s **analog 0–5 V AFR output** through a **10 kΩ / 10 kΩ** divider so the ADC pin stays around **0–2.5 V**. See [wiring.svg](wiring.svg) for a diagram.

Do **not** connect 5 V analog directly to an ESP32 ADC pin.

## Common wire colors

### AEM wideband

| Color | Function |
|-------|----------|
| **Red** | 12V power |
| **Black** | Ground |
| **White** | Analog 0–5V output |
| **Brown** | Sensor ground |

### Innovate Motorsports LC-1 / LC-2

| Color | Function |
|-------|----------|
| **Red** | 12V power |
| **Blue/White** | Ground |
| **Yellow** | Analog 0–5V output |
| **Black** | LED / calibration wire |

> Wire colors can vary by harness revision. Always confirm against the controller’s manual for your exact unit.

## Connections to the ESP32

| Wideband signal | Connect to | Notes |
|-----------------|------------|-------|
| Analog 0–5V (White / Yellow) | 10k/10k divider mid → **GPIO34** (or ADS1115 A0) | Midpoint ≈ 0–2.5 V |
| Ground (Black or Blue/White) | **ESP32 GND** + vehicle ground | Common ground required |
| 12V power (Red) | Vehicle switched +12V | Do **not** feed 12 V into the ESP32 |
| Sensor GND / Cal (Brown / Black) | Per manufacturer | Cal / LED wire is not used by this firmware |

### Recommended divider (both ADC methods)

```
Wideband 0–5 V out ──┬── 10k ── GPIO34 / ADS A0
                     └── 10k ── GND
```

Firmware undoes the divide with `DIVIDER_GAIN = 2.0` in `src/main.cpp`.

### Optional ADS1115 (3.3 V module)

| ADS1115 | Connect to |
|---------|------------|
| VDD | ESP32 3V3 |
| GND | ESP32 GND |
| SDA | GPIO21 |
| SCL | GPIO22 |
| ADDR | GND (I²C address `0x48`) |
| A0 | Divider midpoint |

The phone selects resistor-ADC vs ADS1115 via the BLE hardware-mode characteristic.
