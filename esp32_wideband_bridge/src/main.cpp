// ============================================================================
// R53 Wideband BLE Bridge — Seeed XIAO ESP32S3
// ----------------------------------------------------------------------------
// Streams **sensor volts** (0–5 V) as a 4-byte little-endian float NOTIFY.
// The phone maps volts → AFR (Innovate / AEM / custom).
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
// GATT (must match WidebandBleManager):
//   Service        : 4fafc201-1fb5-459e-8fcc-c5c9c331914b
//   Volts NOTIFY   : beb5483e-36e1-4688-b7f5-ea07361b26a8
//   Hw mode R/W    : beb5483e-36e1-4688-b7f5-ea07361b26a9  (1 byte: 0|1)
//
// Libraries: NimBLE-Arduino 2.x (platformio.ini). ADS1115 path uses Wire only.
// ============================================================================

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Wire.h>
#include <math.h>

static const char* SERVICE_UUID      = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static const char* VOLTS_CHAR_UUID   = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
static const char* HW_MODE_CHAR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a9";

// --- Shared divider (10k/10k → 0–2.5 V at the ADC pin) ----------------------
static const float DIVIDER_GAIN = 2.0f;  // sensor_V = pin_V * DIVIDER_GAIN

// XIAO silkscreen D0 / A0 → GPIO1. Prefer the board's A0 alias when present.
#if defined(A0)
static const int ESP_ADC_PIN = A0;
#else
static const int ESP_ADC_PIN = 1;
#endif
static const float ESP_ADC_REF = 3.3f;
static const int   ESP_ADC_MAX = 4095;

// ADS1115 on XIAO default I²C (D4=SDA, D5=SCL)
static const int     ADS_SDA_PIN = 5;  // D4 / GPIO5
static const int     ADS_SCL_PIN = 6;  // D5 / GPIO6
static const uint8_t ADS_ADDR    = 0x48;
static const float   ADS_LSB_MV  = 0.125f;  // GAIN_ONE (±4.096 V)

enum HwMode : uint8_t {
  HW_RESISTOR = 0,
  HW_ADS1115  = 1,
};

static const uint32_t NOTIFY_PERIOD_MS = 20;

NimBLECharacteristic* voltsCharacteristic  = nullptr;
NimBLECharacteristic* hwModeCharacteristic = nullptr;
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

void setup() {
  analogReadResolution(12);
  // Warm the ADC channel (required before attenuation APIs on arduino-esp32 3.x).
  // Default attenuation is already 11 dB (~0–3.3 V) — fine for a 0–2.5 V mid.
  (void)analogRead(ESP_ADC_PIN);

  NimBLEDevice::init("R53-Wideband");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService* service = server->createService(SERVICE_UUID);

  voltsCharacteristic = service->createCharacteristic(
      VOLTS_CHAR_UUID, NIMBLE_PROPERTY::NOTIFY);

  hwModeCharacteristic = service->createCharacteristic(
      HW_MODE_CHAR_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  hwModeCharacteristic->setCallbacks(new HwModeCallbacks());
  uint8_t hwModeInit = hwMode;
  hwModeCharacteristic->setValue(&hwModeInit, 1);

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
  delay(NOTIFY_PERIOD_MS);
}
