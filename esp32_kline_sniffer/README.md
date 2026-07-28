# ESP32 K-Line Sniffer — passive tap for a genuine ISTA/INPA session

A listen-only ESP32 that taps the R53's K-line (OBD-II pin 7) and prints every
telegram — with microsecond timing — while a **real** tester (ISTA, INPA, or
EDIABAS) talks to the Siemens EMS2K ECU.

> **Listen-only. Never drive the bus.** The ESP32 TX pin is left unwired.
> If the ESP32 transmits while the real tester is mid-session, it corrupts the
> conversation and may confuse the ECU. This tap only *listens*.

## What you need

- An ESP32 dev board (any variant).
- A K-line front-end (resistor divider below, or L9637D).
- An OBD-II breakout / cable, or a pigtail onto pins 7 (K-line), 5 (signal
  ground), and 16 (+12 V battery, only needed for the L9637D).
- A USB cable to your laptop to read the serial output.

Ignition on (engine running or key in position II) so the ECU is powered and
ISTA can connect.

## Front-end A — resistor divider (use this now)

The K-line idles at **~12 V** and pulses low. The ESP32 pin is **3.3 V and NOT
5 V-tolerant**, so we divide it down and clamp it.

```
 OBD pin 7 (K-line) ──[ R1 10k ]──┬──────────────► ESP32 GPIO16 (Serial2 RX)
                                  │
                                  ├───────────────► ESP32 GPIO4  (break detect)
                                  │
                              [ R2 3.3k ]        (clamp) BAT85 Schottky:
                                  │                 anode → this node
 OBD pin 5 (GND) ─────────────────┴── ESP32 GND    cathode → ESP32 3V3
```

- **R1 = 10 kΩ** in series from the K-line, **R2 = 3.3 kΩ** to ground.
- **Clamp** the node to the 3V3 rail with a small Schottky (BAT85/1N5817).
- **Common ground is mandatory:** OBD pin 5 ↔ ESP32 GND.

## Front-end B — L9637D

Proper K-line transceiver. For listen-only: tie **TxD** to 3V3 (do **not** wire
ESP32 TX). Confirm pin numbers against the datasheet.

| L9637D function | Connect to |
|---|---|
| **K** (bus) | OBD pin 7 |
| **VS** (supply) | OBD pin 16 (+12 V) |
| **GND** | OBD pin 5 + ESP32 GND |
| **RxD** (output) | ESP32 GPIO16 + GPIO4 (pull-up to **3V3**) |
| **TxD** (input) | **tie to 3V3** |
| **L** (init line) | unused |

## Flashing

1. Arduino IDE → Boards Manager → install **esp32** (Espressif).
2. Open `esp32_kline_sniffer.ino`. No libraries to add.
3. Serial Monitor at **500000** baud.
4. Upload. Default K-line baud is **9600**; try **10400** if hex looks like garbage.

## Capture procedure

1. Connect the front-end; power the sniffer **first**.
2. Start the ISTA/INPA session so you catch the cold init.
3. Read live values / faults; optionally reset adaptations.
4. Copy the serial log for analysis.

## Output

```
+ 27ms  REQ   B8 13 F7 02 21 18 DC   [7B XOR-ok, read LID 0x18 ...]
+ 31ms  RESP  B8 F7 13 09 61 18 ..   [13B XOR-ok, block 0x18 ...]
>>> LINE-LOW 25.4 ms  (fast-init WAKE break?)
```

## Safety

- **Passive only.** TX never touches the bus.
- Separate from `../esp32_wideband_bridge/` (active BLE AFR sender).
