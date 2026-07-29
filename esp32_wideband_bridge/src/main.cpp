// ============================================================================
// R53 Wideband BLE Bridge — ESP32 reference firmware
// ----------------------------------------------------------------------------
// Streams **sensor volts** (0–5 V) as a 4-byte little-endian float NOTIFY.
// The phone maps volts → AFR (Innovate / AEM / custom).
//
// Hardware methods (phone picks via config WRITE characteristic):
//   0 = Resistor divider → ESP32 ADC (GPIO34) — easiest, no extra chip
//   1 = ADS1115 I²C ADC (A0) — better linearity; still use the same divider
//
// Recommended divider (both methods) — two equal resistors, e.g. 10k + 10k:
//   Wideband 0–5 V out ──┬── 10k ── GPIO34 / ADS A0
//                        └── 10k ── GND
//   Midpoint ≈ 0–2.5 V. DIVIDER_GAIN = 2.0 undoes that.
//
// ADS1115 wiring (3.3 V module):
//   VDD→3V3  GND→GND  SDA→GPIO21  SCL→GPIO22  ADDR→GND (0x48)  A0←divider mid
//
// GATT (must match WidebandBleManager):
//   Service        : 4fafc201-1fb5-459e-8fcc-c5c9c331914b
//   Volts NOTIFY   : beb5483e-36e1-4688-b7f5-ea07361b26a8
//   Hw mode R/W    : beb5483e-36e1-4688-b7f5-ea07361b26a9  (1 byte: 0|1)
//
// Libraries: NimBLE-Arduino (platformio.ini). ADS1115 path uses Wire only.
// ============================================================================

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Wire.h>
#include <math.h>

static const char* SERVICE_UUID     = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static const char* VOLTS_CHAR_UUID  = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
static const char* HW_MODE_CHAR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a9";

// --- Shared divider (10k/10k → 0–2.5 V at the ADC pin) ----------------------
static const float DIVIDER_GAIN = 2.0f;  // sensor_V = pin_V * DIVIDER_GAIN

// --- Method 0: ESP32 ADC ----------------------------------------------------
static const int   ESP_ADC_PIN   = 34;   // ADC1_CH6
static const float ESP_ADC_REF   = 3.3f;
static const int   ESP_ADC_MAX   = 4095;

// --- Method 1: ADS1115 ------------------------------------------------------
static const int   ADS_SDA_PIN   = 21;
static const int   ADS_SCL_PIN   = 22;
static const uint8_t ADS_ADDR    = 0x48;
static const float ADS_LSB_MV    = 0.125f; // GAIN_ONE (±4.096 V)

enum HwMode : uint8_t {
  HW_RESISTOR = 0,
  HW_ADS1115  = 1,
};

static const uint32_t NOTIFY_PERIOD_MS = 20;

NimBLECharacteristic* voltsCharacteristic = nullptr;
NimBLECharacteristic* hwModeCharacteristic = nullptr;
bool clientConnected = false;

volatile uint8_t hwMode = HW_RESISTOR;
bool adsPresent = false;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server) override { clientConnected = true; }
  void onDisconnect(NimBLEServer* server) override {
    clientConnected = false;
    NimBLEDevice::startAdvertising();
  }
};

class HwModeCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string v = c->getValue();
    if (v.empty()) return;
    uint8_t mode = static_cast<uint8_t>(v[0]);
    if (mode == HW_RESISTOR || mode == HW_ADS1115) {
      hwMode = mode;
      c->setValue(&hwMode, 1);
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
  // MUX AIN0|GND, PGA ±4.096, continuous, 128 SPS, comparator off
  const uint16_t config = 0x4283;
  if (!adsWriteReg(0x01, config)) return false;
  delay(10);
  uint16_t check = 0;
  if (!adsReadReg(0x01, &check)) return false;
  return true;
}

float readAdsSensorVolts() {
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
  if (hwMode == HW_ADS1115 && adsPresent) {
    float v = readAdsSensorVolts();
    if (isfinite(v)) return v;
  }
  return readEspAdcSensorVolts();
}

void setup() {
  analogReadResolution(12);

  Wire.begin(ADS_SDA_PIN, ADS_SCL_PIN);
  Wire.setClock(400000);
  adsPresent = adsInit();

  NimBLEDevice::init("R53-Wideband");
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService* service = server->createService(SERVICE_UUID);

  voltsCharacteristic = service->createCharacteristic(
      VOLTS_CHAR_UUID, NIMBLE_PROPERTY::NOTIFY);

  hwModeCharacteristic = service->createCharacteristic(
      HW_MODE_CHAR_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  hwModeCharacteristic->setCallbacks(new HwModeCallbacks());
  hwModeCharacteristic->setValue(&hwMode, 1);

  service->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
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
