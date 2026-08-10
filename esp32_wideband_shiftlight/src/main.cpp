// ============================================================================
// R53 Wideband BLE Bridge + Shift Light — Seeed XIAO ESP32S3 (one box)
// ----------------------------------------------------------------------------
// Merges the wideband bridge and the WS2812B shift light onto a single ESP32.
// The RPM source is the phone: the R53 logger app already reads RPM off the
// K-line and writes it to this device over the SAME BLE connection it uses for
// wideband volts. No ESP-NOW, no Wi-Fi, no second board.
//
//   ECU ── K-line ── phone app ── BLE ──> this box ──> 8x WS2812B shift light
//   wideband 0–5 V ── divider ──> this box ── BLE volts NOTIFY ──> phone app
//
// Wideband side is IDENTICAL to esp32_wideband_bridge (same service, same
// characteristic UUIDs) — an app that only knows the plain bridge still works,
// the LEDs simply stay dark.
//
// Hardware methods (phone picks via config WRITE characteristic):
//   0 = Resistor divider → XIAO ADC (D0 / A0 / GPIO1) — easiest, no extra chip
//   1 = ADS1115 I²C ADC (A0) — better linearity; still use the same divider
//
// Recommended divider (both methods) — two equal resistors, e.g. 10k + 10k:
//   Wideband 0–5 V out ──┬── 10k ── D0 (A0 / GPIO1) / ADS A0
//                        └── 10k ── GND
//   Midpoint ≈ 0–2.5 V. DIVIDER_GAIN = 2.0 undoes that.
//   Do NOT feed 0–5 V straight into the XIAO — ADC max is ~3.3 V.
//
// ADS1115 (optional, 3.3 V module) on XIAO I²C — probed only when mode=1:
//   VDD→3V3  GND→GND  SDA→D4 (GPIO5)  SCL→D5 (GPIO6)  ADDR→GND (0x48)
//   A0←divider mid
//
// Shift light strip (8x WS2812B):
//   5V→strip 5V   GND→strip GND (common with XIAO)   D1 (GPIO2)→strip DIN
//   3.3 V data into a 5 V strip usually works at this length; add a 74AHCT125
//   level shifter if the first LED glitches.
//
// GATT (service + volts/hw-mode must match WidebandBleManager):
//   Service        : 4fafc201-1fb5-459e-8fcc-c5c9c331914b
//   Volts NOTIFY   : beb5483e-36e1-4688-b7f5-ea07361b26a8
//   Hw mode R/W    : beb5483e-36e1-4688-b7f5-ea07361b26a9  (1 byte: 0|1)
//   RPM W/WNR      : beb5483e-36e1-4688-b7f5-ea07361b26aa  (u16 LE rpm)
//   Shift cfg R/W  : beb5483e-36e1-4688-b7f5-ea07361b26ab
//                    (u16 LE green-start, u16 LE fade-start, u16 LE blink,
//                     u8 brightness — 7 bytes, all-or-nothing)
//
// RPM is treated as a live signal, not a value: if the app stops writing for
// RPM_STALE_MS (app killed, BLE drop, logging stopped) the strip goes dark
// rather than holding the last state.
//
// Libraries: NimBLE-Arduino 2.x + FastLED 3.7+ (platformio.ini).
// ============================================================================

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Wire.h>
#include <FastLED.h>
#include <math.h>

static const char* SERVICE_UUID        = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static const char* VOLTS_CHAR_UUID     = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
static const char* HW_MODE_CHAR_UUID   = "beb5483e-36e1-4688-b7f5-ea07361b26a9";
static const char* RPM_CHAR_UUID       = "beb5483e-36e1-4688-b7f5-ea07361b26aa";
static const char* SHIFT_CFG_CHAR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26ab";

// --- Wideband (unchanged from esp32_wideband_bridge) ------------------------
static const float DIVIDER_GAIN = 2.0f;  // sensor_V = pin_V * DIVIDER_GAIN

#if defined(A0)
static const int ESP_ADC_PIN = A0;       // XIAO D0 / A0 / GPIO1
#else
static const int ESP_ADC_PIN = 1;
#endif
static const float ESP_ADC_REF = 3.3f;
static const int   ESP_ADC_MAX = 4095;

static const int     ADS_SDA_PIN = 5;    // D4 / GPIO5
static const int     ADS_SCL_PIN = 6;    // D5 / GPIO6
static const uint8_t ADS_ADDR    = 0x48;
static const float   ADS_LSB_MV  = 0.125f;  // GAIN_ONE (±4.096 V)

enum HwMode : uint8_t {
  HW_RESISTOR = 0,
  HW_ADS1115  = 1,
};

static const uint32_t NOTIFY_PERIOD_MS = 20;

// --- Shift light -------------------------------------------------------------
#define LED_PIN     2            // XIAO D1 / GPIO2 (D0 is the wideband ADC)
#define NUM_LEDS    8
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

static const uint32_t RPM_STALE_MS   = 1000;  // no RPM write this long → dark
static const uint32_t BLINK_INTERVAL = 100;   // 5 Hz over-redline blink

// Defaults match the standalone shift light; the app can rewrite them live
// (e.g. after a redline-raise flash) via the shift-config characteristic.
struct ShiftConfig {
  uint16_t greenStartRpm = 3000;  // below: dark
  uint16_t fadeStartRpm  = 6000;  // green→red fade begins
  uint16_t blinkRpm      = 7100;  // at/above: all-red 5 Hz blink
  uint8_t  brightness    = 75;    // FastLED 0–255
};

CRGB leds[NUM_LEDS];
volatile ShiftConfig shiftCfg;
volatile uint16_t rpm = 0;
volatile uint32_t lastRpmMs = 0;
bool redBlinkState = false;
uint32_t lastBlinkTime = 0;

NimBLECharacteristic* voltsCharacteristic = nullptr;
bool clientConnected = false;

volatile uint8_t hwMode = HW_RESISTOR;
bool adsPresent = false;
bool adsTried   = false;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* /*server*/, NimBLEConnInfo& /*connInfo*/) override {
    clientConnected = true;
  }
  void onDisconnect(NimBLEServer* /*server*/, NimBLEConnInfo& /*connInfo*/,
                    int /*reason*/) override {
    clientConnected = false;
    rpm = 0;  // never hold a shift indication with nobody driving it
    NimBLEDevice::startAdvertising();
  }
};

class HwModeCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*connInfo*/) override {
    NimBLEAttValue v = c->getValue();
    if (v.length() < 1) return;
    uint8_t mode = v[0];
    if (mode == HW_RESISTOR || mode == HW_ADS1115) {
      hwMode = mode;
      uint8_t value = hwMode;
      c->setValue(&value, 1);
    }
  }
};

class RpmCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*connInfo*/) override {
    NimBLEAttValue v = c->getValue();
    if (v.length() < 2) return;
    rpm = (uint16_t)v[0] | ((uint16_t)v[1] << 8);
    lastRpmMs = millis();
  }
};

class ShiftCfgCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*connInfo*/) override {
    NimBLEAttValue v = c->getValue();
    if (v.length() < 7) return;
    uint16_t green = (uint16_t)v[0] | ((uint16_t)v[1] << 8);
    uint16_t fade  = (uint16_t)v[2] | ((uint16_t)v[3] << 8);
    uint16_t blink = (uint16_t)v[4] | ((uint16_t)v[5] << 8);
    uint8_t  bri   = v[6];
    // All-or-nothing sanity: ordered thresholds, plausible rpm, visible LEDs.
    if (!(green < fade && fade < blink && blink <= 9000 && bri >= 1)) return;
    shiftCfg.greenStartRpm = green;
    shiftCfg.fadeStartRpm  = fade;
    shiftCfg.blinkRpm      = blink;
    shiftCfg.brightness    = bri;
    FastLED.setBrightness(bri);
    uint8_t echo[7] = { v[0], v[1], v[2], v[3], v[4], v[5], v[6] };
    c->setValue(echo, sizeof(echo));
  }
};

float readEspAdcSensorVolts() {
  int raw = analogRead(ESP_ADC_PIN);
  float pinVolts = (raw / (float)ESP_ADC_MAX) * ESP_ADC_REF;
  float sensorVolts = pinVolts * DIVIDER_GAIN;
  if (sensorVolts < 0.0f) sensorVolts = 0.0f;
  if (sensorVolts > 5.5f) sensorVolts = 5.5f;
  return sensorVolts;
}

bool adsWriteReg(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(ADS_ADDR);
  Wire.write(reg);
  Wire.write(static_cast<uint8_t>(value >> 8));
  Wire.write(static_cast<uint8_t>(value & 0xFF));
  return Wire.endTransmission() == 0;
}

bool adsReadReg(uint8_t reg, uint16_t* out) {
  Wire.beginTransmission(ADS_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ADS_ADDR, (uint8_t)2) != 2) return false;
  uint16_t hi = Wire.read();
  uint16_t lo = Wire.read();
  *out = (hi << 8) | lo;
  return true;
}

/** Probe + configure continuous AIN0, ±4.096 V, 128 SPS. */
bool adsInit() {
  const uint16_t config = 0x4283;
  if (!adsWriteReg(0x01, config)) return false;
  delay(10);
  uint16_t check = 0;
  if (!adsReadReg(0x01, &check)) return false;
  return true;
}

/** Lazy I²C — only touch the bus when the phone asks for ADS1115 mode. */
void ensureAdsProbed() {
  if (adsTried) return;
  adsTried = true;
  Wire.begin(ADS_SDA_PIN, ADS_SCL_PIN);
  Wire.setClock(400000);
  adsPresent = adsInit();
}

float readAdsSensorVolts() {
  ensureAdsProbed();
  if (!adsPresent) return NAN;
  uint16_t raw = 0;
  if (!adsReadReg(0x00, &raw)) return NAN;
  int16_t signedRaw = static_cast<int16_t>(raw);
  float pinVolts = (signedRaw * ADS_LSB_MV) / 1000.0f;
  float sensorVolts = pinVolts * DIVIDER_GAIN;
  if (sensorVolts < 0.0f) sensorVolts = 0.0f;
  if (sensorVolts > 5.5f) sensorVolts = 5.5f;
  return sensorVolts;
}

float readSensorVolts() {
  if (hwMode == HW_ADS1115) {
    float v = readAdsSensorVolts();
    if (isfinite(v)) return v;
  }
  return readEspAdcSensorVolts();
}

/** Ported from the standalone shift light: pairs light from the ends inward. */
void updateLEDs() {
  uint16_t r = rpm;
  if (r != 0 && (uint32_t)(millis() - lastRpmMs) > RPM_STALE_MS) {
    rpm = 0;
    r = 0;
  }

  CRGB color;
  int numPairs = constrain(map((long)r, 0, (long)shiftCfg.blinkRpm, 0, 4), 0, 4);

  if (r < shiftCfg.greenStartRpm) {
    color = CRGB(0, 0, 0);
  } else if (r < shiftCfg.fadeStartRpm) {
    color = CRGB(0, 255, 0);
  } else if (r < shiftCfg.blinkRpm) {
    uint8_t t = map((long)r, (long)shiftCfg.fadeStartRpm, (long)shiftCfg.blinkRpm, 0, 255);
    color = CRGB(t, 255 - t, 0);
  } else {
    if (millis() - lastBlinkTime >= BLINK_INTERVAL) {
      redBlinkState = !redBlinkState;
      lastBlinkTime = millis();
    }
    color = redBlinkState ? CRGB(255, 0, 0) : CRGB(0, 0, 0);
    numPairs = 4;
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(0, 0, 0);
    if (numPairs >= 1 && (i == 0 || i == 7)) leds[i] = color;  // ends
    if (numPairs >= 2 && (i == 1 || i == 6)) leds[i] = color;
    if (numPairs >= 3 && (i == 2 || i == 5)) leds[i] = color;
    if (numPairs >= 4 && (i == 3 || i == 4)) leds[i] = color;  // center
  }
}

void setup() {
  analogReadResolution(12);
  // Warm the ADC channel (required before attenuation APIs on arduino-esp32 3.x).
  (void)analogRead(ESP_ADC_PIN);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(shiftCfg.brightness);
  FastLED.clear(true);

  NimBLEDevice::init("R53-Wideband");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService* service = server->createService(SERVICE_UUID);

  voltsCharacteristic = service->createCharacteristic(
      VOLTS_CHAR_UUID, NIMBLE_PROPERTY::NOTIFY);

  NimBLECharacteristic* hwModeCharacteristic = service->createCharacteristic(
      HW_MODE_CHAR_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  hwModeCharacteristic->setCallbacks(new HwModeCallbacks());
  uint8_t hwModeInit = hwMode;
  hwModeCharacteristic->setValue(&hwModeInit, 1);

  NimBLECharacteristic* rpmCharacteristic = service->createCharacteristic(
      RPM_CHAR_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rpmCharacteristic->setCallbacks(new RpmCallbacks());

  NimBLECharacteristic* shiftCfgCharacteristic = service->createCharacteristic(
      SHIFT_CFG_CHAR_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  shiftCfgCharacteristic->setCallbacks(new ShiftCfgCallbacks());
  uint8_t cfgInit[7] = {
    (uint8_t)(shiftCfg.greenStartRpm & 0xFF), (uint8_t)(shiftCfg.greenStartRpm >> 8),
    (uint8_t)(shiftCfg.fadeStartRpm & 0xFF),  (uint8_t)(shiftCfg.fadeStartRpm >> 8),
    (uint8_t)(shiftCfg.blinkRpm & 0xFF),      (uint8_t)(shiftCfg.blinkRpm >> 8),
    shiftCfg.brightness,
  };
  shiftCfgCharacteristic->setValue(cfgInit, sizeof(cfgInit));

  service->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->enableScanResponse(true);
  adv->start();
}

void loop() {
  if (clientConnected && voltsCharacteristic != nullptr) {
    float volts = readSensorVolts();
    if (isfinite(volts)) {
      voltsCharacteristic->setValue(reinterpret_cast<uint8_t*>(&volts), sizeof(volts));
      voltsCharacteristic->notify();
    }
  }
  updateLEDs();
  FastLED.show();
  delay(NOTIFY_PERIOD_MS);
}
