// ============================================================================
// R53 K-Line Sniffer — ESP32 passive listen-only firmware
// ----------------------------------------------------------------------------
// Eavesdrops on the single-wire K-line (OBD-II pin 7) while a *genuine* tester
// (ISTA / INPA / EDIABAS) talks to the Siemens EMS2K ECU, and prints every
// telegram — with microsecond timing — over USB serial so it can be pasted
// back for analysis.
//
// The K-line is HALF-DUPLEX: tester and ECU share one wire, so a passive tap
// sees BOTH sides. We reconstruct who-said-what from the frame's target byte.
//
// >>> LISTEN-ONLY. The ESP32 UART TX pin is NEVER wired to the bus. <<<
// Driving the line would corrupt the real tester's session. RX only.
//
// Goal: capture the cold-start INIT handshake (wake-up pulse +
// StartCommunication) and its exact timing.
//
// Board: any ESP32 dev board. No libraries required (core only).
// Front-end: conditioned K-line signal into RX_PIN (see README).
// Wire the SAME conditioned signal to BREAK_PIN too, for init-pulse timing.
// ============================================================================

#include <Arduino.h>

// --- Configuration ----------------------------------------------------------

// EMS2K live-data baud is 9600. If every capture looks like garbage hex,
// try 10400 (generic ISO9141-2 mode).
static const uint32_t KLINE_BAUD = 9600;

// USB serial back to the laptop. Keep it fast so printing never lags the bus.
static const uint32_t USB_BAUD = 500000;

// Serial2 RX. GPIO16 is the ESP32 default; check your board's silkscreen.
// TX (GPIO17) is deliberately left UNUSED and UNWIRED — passive tap only.
static const int RX_PIN = 16;
static const int TX_PIN = 17;  // not connected to the bus; declared for begin()

// Optional init-pulse timing: jumper the conditioned K-line signal to this pin
// too. A wake-up "break" is a long line-low (~25 ms for fast init; the 5-baud
// address bits are ~200 ms each). Normal data lows never exceed ~1 ms at 9600,
// so anything longer than BREAK_MIN_US is a genuine init event, not a byte.
static const bool ENABLE_BREAK_DETECT = true;
static const int  BREAK_PIN    = 4;
static const uint32_t BREAK_MIN_US = 3000;  // 3 ms — well above a data low

// A telegram is one "burst" of bytes. Within a tester request, EDIABAS paces
// bytes ~5 ms apart; between telegrams the P2/P3 gap is >=25 ms. Split bursts
// on a gap larger than this so each printed line is exactly one telegram.
static const uint32_t FRAME_GAP_US = 15000;  // 15 ms

// --- EMS2K framing constants ------------------------------------------------
static const uint8_t FMT_B8     = 0xB8;
static const uint8_t ADDR_ECU   = 0x13;  // target on a tester->ECU request
static const uint8_t ADDR_TESTER= 0xF7;  // target on an ECU->tester response

// --- Burst buffer -----------------------------------------------------------
static const size_t   BURST_MAX = 300;
static uint8_t        burst[BURST_MAX];
static size_t         burstLen = 0;
static uint32_t       burstStartUs = 0;
static uint32_t       lastByteUs   = 0;
static uint32_t       prevBurstEndUs = 0;
static bool           haveByte = false;

// --- Break detector (ISR) ---------------------------------------------------
volatile uint32_t     isrFallUs      = 0;
volatile uint32_t     isrBreakDurUs  = 0;
volatile bool         isrBreakPending= false;

void IRAM_ATTR onBreakEdge() {
  // The conditioned signal is non-inverting: bus idle = HIGH, active = LOW.
  if (digitalRead(BREAK_PIN) == LOW) {
    isrFallUs = micros();
  } else {
    uint32_t dur = micros() - isrFallUs;
    if (dur >= BREAK_MIN_US) {
      isrBreakDurUs = dur;
      isrBreakPending = true;
    }
  }
}

// --- Helpers ----------------------------------------------------------------

const char* lidName(uint8_t lid) {
  switch (lid) {
    case 0x11: return "STATUS_ANA_ENGINE2";
    case 0x12: return "STATUS_ANA_ENGINE3";
    case 0x13: return "STATUS_ANA_ENGINE4 (MAP/temps/throttle)";
    case 0x14: return "STATUS_ANA_ENGINE5 (spark/knock)";
    case 0x16: return "STATUS_ANA_ENGINE7 (MAF)";
    case 0x17: return "lambda / fuel-trim block";
    case 0x18: return "STATUS_ANA_ENGINE9 (RPM/battery/load)";
    case 0x19: return "STATUS_ANA_INJ_TIME";
    case 0x27: return "STATUS_ANA_BASE_ENGINE (torque)";
    default:   return "";
  }
}

uint8_t xorFold(const uint8_t* p, size_t n) {
  uint8_t x = 0;
  for (size_t i = 0; i < n; i++) x ^= p[i];
  return x;
}

void printHex(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (p[i] < 0x10) Serial.print('0');
    Serial.print(p[i], HEX);
    if (i + 1 < n) Serial.print(' ');
  }
}

void flushBurst() {
  if (burstLen == 0) return;

  uint32_t gapMs = (prevBurstEndUs == 0) ? 0 : (burstStartUs - prevBurstEndUs) / 1000;

  Serial.print('+');
  if (gapMs < 100) Serial.print(' ');
  if (gapMs < 10)  Serial.print(' ');
  Serial.print(gapMs);
  Serial.print("ms  ");

  const char* dir = "????";
  const char* note = "";
  bool xorOk = false;
  char lidBuf[64] = {0};

  if (burstLen >= 5 && burst[0] == FMT_B8) {
    uint8_t tgt = burst[1];
    uint8_t len = burst[3];
    size_t  frameLen = 4 + len + 1;
    if (frameLen == burstLen) {
      xorOk = (xorFold(burst, burstLen - 1) == burst[burstLen - 1]);
    }
    if (tgt == ADDR_ECU)         dir = "REQ ";
    else if (tgt == ADDR_TESTER) dir = "RESP";

    uint8_t sid = burst[4];
    if (tgt == ADDR_ECU) {
      if (sid == 0x21 && burstLen >= 6) {
        snprintf(lidBuf, sizeof(lidBuf), "read LID 0x%02X %s", burst[5], lidName(burst[5]));
        note = lidBuf;
      } else if (sid == 0x81) {
        note = "StartCommunication";
      } else if (sid == 0x1A) {
        note = "ReadEcuIdentification";
      } else if (sid == 0x10) {
        note = "StartDiagnosticSession";
      } else if (sid == 0x27) {
        note = "SecurityAccess";
      }
    } else if (tgt == ADDR_TESTER) {
      if (sid == 0x61 && burstLen >= 6) {
        snprintf(lidBuf, sizeof(lidBuf), "block 0x%02X %s", burst[5], lidName(burst[5]));
        note = lidBuf;
      } else if (sid == 0x7F) {
        note = "NEGATIVE response";
      } else if (sid == 0xC1) {
        note = "StartCommunication +resp (keybytes follow)";
      }
    }
  } else {
    dir = "RAW ";
  }

  Serial.print(dir);
  Serial.print("  ");
  printHex(burst, burstLen);

  Serial.print("   [");
  Serial.print(burstLen);
  Serial.print("B");
  if (burst[0] == FMT_B8) Serial.print(xorOk ? " XOR-ok" : " XOR-BAD");
  if (note[0]) { Serial.print(", "); Serial.print(note); }
  Serial.println(']');

  prevBurstEndUs = lastByteUs;
  burstLen = 0;
}

void setup() {
  Serial.begin(USB_BAUD);
  delay(200);

  Serial.println();
  Serial.println("=== R53 K-Line Sniffer (passive, listen-only) ===");
  Serial.print("K-line baud: ");
  Serial.print(KLINE_BAUD);
  Serial.println(" 8N1, RX only (TX unwired).");
  Serial.println("Power this up FIRST, then start ISTA/INPA to catch the init.");
  Serial.println("Columns:  +<gap>ms  DIR  <hex bytes>  [len, XOR, note]");
  Serial.println("=================================================");

  Serial2.begin(KLINE_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);

  if (ENABLE_BREAK_DETECT) {
    pinMode(BREAK_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(BREAK_PIN), onBreakEdge, CHANGE);
  }
}

void loop() {
  uint32_t now = micros();

  if (isrBreakPending) {
    noInterrupts();
    uint32_t dur = isrBreakDurUs;
    isrBreakPending = false;
    interrupts();
    flushBurst();
    Serial.print(">>> LINE-LOW ");
    Serial.print(dur / 1000.0, 1);
    Serial.print(" ms  (");
    if (dur > 150000)      Serial.print("5-baud init bit?");
    else if (dur > 20000)  Serial.print("fast-init WAKE break?");
    else                   Serial.print("long low / gap");
    Serial.println(")");
    prevBurstEndUs = now;
  }

  while (Serial2.available()) {
    uint8_t b = (uint8_t)Serial2.read();
    now = micros();
    if (burstLen == 0) burstStartUs = now;
    if (burstLen < BURST_MAX) burst[burstLen++] = b;
    lastByteUs = now;
    haveByte = true;
  }

  if (haveByte && burstLen > 0 && (now - lastByteUs) > FRAME_GAP_US) {
    flushBurst();
    haveByte = false;
  }
}
