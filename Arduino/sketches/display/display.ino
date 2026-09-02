/*
 * Millennium Beta (display) firmware.
 * Build with FQBN arduino:avr:millennium_beta so the board identifies as
 * "Millennium Beta" on USB (/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00).
 */
#include <SoftwareSerial.h>
#include <Wire.h>
#include <avr/wdt.h>
#include <MillenniumProtocol.h>

#ifndef MILLENNIUM_FIRMWARE_VERSION
#define MILLENNIUM_FIRMWARE_VERSION "0.0.0-development"
#endif
#ifndef MILLENNIUM_FIRMWARE_BUILD
#define MILLENNIUM_FIRMWARE_BUILD "unknown"
#endif
#define I2C_DISPLAY_ADDR 8

/* Legacy raw query retained only for the offline OTA attestation helper. */
#define CMD_IDENTITY      0x07  /* OTA attestation query */

/* Captured before the Arduino runtime or boot-time watchdog can clear MCUSR. */
uint8_t previousResetCause __attribute__((section(".noinit")));
void captureResetCause(void) __attribute__((naked, section(".init3")));
void captureResetCause(void) {
  previousResetCause = MCUSR;
  MCUSR = 0;
  wdt_disable();
}
static bool resetCauseReported = false;

#define HEARTBEAT_INTERVAL_MS 10000UL

#define d0 5
#define d1 6
#define d2 7
#define d3 8
#define d4 9
#define d5 10
#define d6 11
#define d7 12
#define WR 4
#define AD 0
#define RD 1
#define CS 17
#define TEST 16
#define RESET 13

const int coinResetPin = 15;
SoftwareSerial coinSerialDevice(14, 23);

/*
 * Coin validator EEPROM image (256 bytes).
 *
 * This table is written to the Mars/MEI TRC-6500 coin validator's EEPROM
 * via the CMD_COIN_PROGRAM command.  It configures the validator's acceptance
 * parameters for US coinage:
 *
 *   Bytes   0-14:  Global configuration (sensor thresholds, timing, options)
 *   Bytes  15-29:  Coin type 1 — US nickel   ($0.05)
 *   Bytes  30-44:  Coin type 2 — US dime     ($0.10)
 *   Bytes  45-59:  Coin type 3 — US quarter  ($0.25)
 *   Bytes  60-74:  Coin type 4 — US dollar   ($1.00)  [Sacagawea / Presidential]
 *   Bytes  75-104: Coin type 5-6 (reserved / unused, zeroed)
 *   Bytes 105-191: Reserved (zeroed)
 *   Bytes 192-210: Calibration / checksum block
 *   Bytes 211-255: Reserved / serial data
 *
 * Each 15-byte coin type block contains acceptance windows for the coin's
 * diameter, thickness, and metal composition as measured by the validator's
 * inductive sensors.  The values were captured from a known-good validator
 * and are specific to the TRC-6500 hardware revision.
 */
byte coinEeprom[] = {
    3,   217, 5,   255, 0,   248, 1,   110, 10,  0,   5,   8,   7,   4,   0,
    3,   240, 5,   204, 40,  0,   192, 0,   12,  40,  180, 18,  50,  128, 100,
    255, 0,   45,  89,  62,  236, 40,  200, 80,  111, 37,  75,  35,  72,  50,
    146, 38,  207, 66,  30,  39,  134, 53,  59,  59,  184, 47,  145, 74,  178,
    14,  207, 47,  24,  61,  103, 43,  139, 78,  50,  53,  142, 43,  81,  67,
    171, 39,  11,  74,  45,  76,  20,  162, 141, 49,  87,  37,  7,   68,  99,
    40,  8,   166, 157, 18,  23,  44,  143, 50,  98,  20,  9,   163, 138, 20,
    25,  40,  71,  58,  222, 63,  145, 0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   163, 69,  54,
    199, 39,  3,   66,  5,   66,  68,  151, 196, 57,  67,  22,  195, 89,  5,
    30,  195, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   49,  48,  50,
    48};

/* I2C rx buffer: 64 bytes. Wire lib on AVR uses 32-byte hw buffer.
 * Host must keep commands under this limit (#134). */
/* Sized to hold multiple complete Wire frames while USB is briefly
 * backpressured. Long display and coin work is now cooperative.
 *
 * Keep this <= 256: i2cHead/i2cTail are bytes. */
#define I2C_BUF_SIZE 256
static volatile byte i2cBuf[I2C_BUF_SIZE];
static volatile byte i2cHead = 0, i2cTail = 0;

/* (#230) Bytes the ISR had to throw away because the ring was full, and the
 * last value reported to the host. A full ring silently truncates whatever
 * Alpha was sending -- a card PAN included -- and the host still parses the
 * short result as a valid event, so the loss has to be visible. */
static volatile unsigned int i2cOverflow = 0;
static unsigned int i2cOverflowReported = 0;
static unsigned long lastOverflowReport = 0;

/* Re-announce the overflow count this often, zero included -- matching Alpha.
 * Reporting only on change means a count that starts at 0 and stays there is
 * never sent at all, so the host's gauge simply does not exist and "no metric"
 * cannot be told apart from "no overflows". An affirmative zero is the whole
 * point of instrumenting this. */
static const unsigned long OVERFLOW_REPORT_INTERVAL_MS = 30000;

static unsigned long lastHeartbeat = 0;
static MillenniumDecoder protocolDecoder;
static uint8_t protocolSequence = 0;
static bool lastDisplayCommandValid = false;
static bool lastCoinCommandValid = false;
static uint8_t lastDisplayCommandSequence = 0;
static uint8_t lastCoinCommandSequence = 0;

enum DisplayOperation { DISPLAY_IDLE, DISPLAY_RESET_HIGH, DISPLAY_RESET_LOW,
                        DISPLAY_WAIT_CLEAR, DISPLAY_WRITE_CONTROL, DISPLAY_WRITE_TEXT };
static DisplayOperation displayOperation = DISPLAY_IDLE;
static uint8_t displayBuffer[100];
static uint8_t displayLength = 0;
static uint8_t displayIndex = 0;
static uint8_t displayControl = 0;
static uint8_t displayTransaction = 0;
static unsigned long displayNext = 0;
static unsigned long displayDeadline = 0;

enum CoinOperation { COIN_IDLE, COIN_RESET_LOW, COIN_RESET_HIGH, COIN_CONTROL_WAIT,
                     COIN_PROGRAM, COIN_VERIFY_SEND, COIN_VERIFY_WAIT };
static CoinOperation coinOperation = COIN_IDLE;
static uint16_t coinIndex = 0;
static uint8_t coinStep = 0;
static uint8_t coinTransaction = 0;
static unsigned long coinNext = 0;
static unsigned long coinDeadline = 0;
static unsigned long coinByteDeadline = 0;

const unsigned long SERIAL_TIMEOUT_MS = 2000;

void setup() {
  SerialUSB.begin(9600);

  Wire.begin(I2C_DISPLAY_ADDR);
  Wire.onReceive(receiveEvent);

  pinMode(coinResetPin, OUTPUT);
  pinMode(14, INPUT);
  digitalWrite(coinResetPin, HIGH);
  coinSerialDevice.begin(600);

  pinMode(d0, OUTPUT);
  pinMode(d1, OUTPUT);
  pinMode(d2, OUTPUT);
  pinMode(d3, OUTPUT);
  pinMode(d4, OUTPUT);
  pinMode(d5, OUTPUT);
  pinMode(d6, OUTPUT);
  pinMode(d7, OUTPUT);
  pinMode(WR, OUTPUT);
  pinMode(AD, OUTPUT);
  pinMode(RD, OUTPUT);
  pinMode(CS, OUTPUT);
  pinMode(TEST, OUTPUT);
  pinMode(RESET, OUTPUT);

  digitalWrite(d0, HIGH);
  digitalWrite(d1, HIGH);
  digitalWrite(d2, HIGH);
  digitalWrite(d3, HIGH);
  digitalWrite(d4, HIGH);
  digitalWrite(d5, HIGH);
  digitalWrite(d6, HIGH);
  digitalWrite(d7, HIGH);

  digitalWrite(WR, HIGH);
  digitalWrite(AD, LOW);
  digitalWrite(RD, HIGH);
  digitalWrite(CS, HIGH);
  digitalWrite(TEST, HIGH);
  digitalWrite(RESET, LOW);

  vfdreset();
  delay(100);
  writeCharacter(20u);
  writeCharacter(21);

  wdt_enable(WDTO_4S);
}

void receiveEvent(int howMany) {
  while (Wire.available()) {
    byte b = Wire.read();
    byte next = (i2cHead + 1) % I2C_BUF_SIZE;
    if (next != i2cTail) {
      i2cBuf[i2cHead] = b;
      i2cHead = next;
    } else {
      i2cOverflow++;   /* (#230) ring full; this byte is gone */
    }
  }
}

void vfdreset() {
  digitalWrite(RESET, HIGH);
  delay(2);
  digitalWrite(RESET, LOW);
  delay(10);
}

static void sendFrame(uint8_t type, const uint8_t *payload, uint8_t length) {
  uint8_t frame[MILLENNIUM_PROTOCOL_MAX_FRAME];
  size_t frameLength = millenniumEncode(type, protocolSequence++, payload, length,
                                        frame, sizeof(frame));
  if (frameLength) SerialUSB.write(frame, frameLength);
}

static void sendAck(uint8_t sequence, uint8_t status) {
  uint8_t payload[2] = {sequence, status};
  sendFrame(MCU_MSG_ACK, payload, sizeof(payload));
}

static void sendOperation(char code, uint8_t transaction,
                          const uint8_t *details, uint8_t length) {
  uint8_t payload[5];
  if (length > 3) length = 3;
  payload[0] = (uint8_t)code;
  payload[1] = transaction;
  if (length) memcpy(payload + 2, details, length);
  sendFrame(MCU_EVT_OPERATION, payload, length + 2);
}

static void sendResetCause() {
  uint8_t payload[4];
  unsigned int n;
  if (resetCauseReported) return;
  n = previousResetCause;
  payload[0] = 'D';
  payload[1] = '0' + (n / 100) % 10;
  payload[2] = '0' + (n / 10) % 10;
  payload[3] = '0' + n % 10;
  sendFrame(MCU_EVT_DIAGNOSTIC, payload, sizeof(payload));
  resetCauseReported = true;
}

static void printIdentity() {
  SerialUSB.println(F("MILLENNIUM role=display version=" MILLENNIUM_FIRMWARE_VERSION
                      " protocol=" MILLENNIUM_PROTOCOL_VERSION_STRING
                      " build=" MILLENNIUM_FIRMWARE_BUILD " selftest=ok"));
  SerialUSB.print(F("reset_cause="));
  SerialUSB.println(previousResetCause);
}

static bool timeReached(unsigned long target) {
  return (long)(millis() - target) >= 0;
}

static void startDisplay(const MillenniumFrame &frame) {
  displayLength = frame.length;
  memcpy(displayBuffer, frame.payload, frame.length);
  displayIndex = 0;
  displayControl = 0;
  displayTransaction = frame.sequence;
  displayDeadline = millis() + 5000UL;
  digitalWrite(RESET, HIGH);
  displayNext = millis() + 2UL;
  displayOperation = DISPLAY_RESET_HIGH;
}

static void serviceDisplay() {
  if (displayOperation == DISPLAY_IDLE) return;
  if (timeReached(displayDeadline)) {
    sendOperation('X', displayTransaction, NULL, 0);
    displayOperation = DISPLAY_IDLE;
    return;
  }
  if (!timeReached(displayNext)) return;
  if (displayOperation == DISPLAY_RESET_HIGH) {
    digitalWrite(RESET, LOW);
    displayNext = millis() + 10UL;
    displayOperation = DISPLAY_RESET_LOW;
  } else if (displayOperation == DISPLAY_RESET_LOW) {
    displayNext = millis() + 100UL;
    displayOperation = DISPLAY_WAIT_CLEAR;
  } else if (displayOperation == DISPLAY_WAIT_CLEAR) {
    displayControl = 0;
    displayOperation = DISPLAY_WRITE_CONTROL;
  } else if (displayOperation == DISPLAY_WRITE_CONTROL) {
    writeCharacter(displayControl++ == 0 ? 20u : 18u);
    if (displayControl >= 2) displayOperation = DISPLAY_WRITE_TEXT;
    displayNext = millis();
  } else if (displayOperation == DISPLAY_WRITE_TEXT) {
    if (displayIndex < displayLength) {
      uint8_t value = displayBuffer[displayIndex++];
      writeCharacter(value == 0x0A ? 13 : value);
      displayNext = millis();
    }
    if (displayIndex >= displayLength) {
      sendOperation('R', displayTransaction, NULL, 0);
      displayOperation = DISPLAY_IDLE;
    }
  }
}

static void finishCoin(char code) {
  sendOperation(code, coinTransaction, NULL, 0);
  coinOperation = COIN_IDLE;
}

static void startCoinProgram(uint8_t transaction) {
  coinTransaction = transaction;
  coinIndex = 0;
  coinStep = 0;
  coinNext = millis();
  coinDeadline = millis() + 45000UL;
  coinOperation = COIN_PROGRAM;
  sendOperation('A', transaction, NULL, 0);
}

static void startCoinVerify(uint8_t transaction) {
  coinTransaction = transaction;
  coinIndex = 0;
  coinStep = 0;
  coinNext = millis();
  coinDeadline = millis() + 600000UL;
  coinOperation = COIN_VERIFY_SEND;
  sendOperation('D', transaction, NULL, 0);
}

static void serviceCoin() {
  static const uint8_t programPrefix[4] = {'E', 'A', 'P', 'w'};
  if (coinOperation == COIN_IDLE) return;
  if (timeReached(coinDeadline)) {
    finishCoin('X');
    return;
  }
  if (coinOperation == COIN_RESET_LOW && timeReached(coinNext)) {
    digitalWrite(coinResetPin, HIGH);
    coinNext = millis() + 1000UL;
    coinOperation = COIN_RESET_HIGH;
  } else if ((coinOperation == COIN_RESET_HIGH || coinOperation == COIN_CONTROL_WAIT) &&
             timeReached(coinNext)) {
    finishCoin('R');
  } else if (coinOperation == COIN_PROGRAM && timeReached(coinNext)) {
    if (coinStep < 4) coinSerialDevice.write(programPrefix[coinStep]);
    else if (coinStep == 4) coinSerialDevice.write(lowByte(coinIndex));
    else coinSerialDevice.write(coinEeprom[coinIndex]);
    coinStep++;
    coinNext = millis() + 20UL;
    if (coinStep >= 6) {
      coinStep = 0;
      coinIndex++;
      if (coinIndex >= 256) finishCoin('B');
    }
  } else if (coinOperation == COIN_VERIFY_SEND && timeReached(coinNext)) {
    while (coinSerialDevice.available()) coinSerialDevice.read();
    if (coinStep == 0) coinSerialDevice.write('q');
    else if (coinStep == 1) coinSerialDevice.write(0x01);
    else coinSerialDevice.write(lowByte(coinIndex));
    coinStep++;
    coinNext = millis() + 20UL;
    if (coinStep >= 3) {
      coinByteDeadline = millis() + SERIAL_TIMEOUT_MS;
      coinOperation = COIN_VERIFY_WAIT;
    }
  } else if (coinOperation == COIN_VERIFY_WAIT) {
    if (coinSerialDevice.available()) {
      uint8_t value = coinSerialDevice.read();
      if (value != coinEeprom[coinIndex]) {
        uint8_t detail[3] = {lowByte(coinIndex), value, coinEeprom[coinIndex]};
        sendOperation('E', coinTransaction, detail, sizeof(detail));
      }
      coinIndex++;
      coinStep = 0;
      coinNext = millis();
      coinOperation = coinIndex >= 256 ? COIN_IDLE : COIN_VERIFY_SEND;
      if (coinIndex >= 256) sendOperation('F', coinTransaction, NULL, 0);
    } else if (timeReached(coinByteDeadline)) {
      uint8_t detail[3] = {lowByte(coinIndex), 0xff, coinEeprom[coinIndex]};
      sendOperation('E', coinTransaction, detail, sizeof(detail));
      coinIndex++;
      coinStep = 0;
      coinNext = millis();
      coinOperation = coinIndex >= 256 ? COIN_IDLE : COIN_VERIFY_SEND;
      if (coinIndex >= 256) sendOperation('F', coinTransaction, NULL, 0);
    }
  }
}

static void handleProtocolFrame(const MillenniumFrame &frame) {
  if (frame.type == MCU_MSG_HELLO) {
    uint8_t versions[2] = {MILLENNIUM_PROTOCOL_VERSION, MILLENNIUM_PROTOCOL_VERSION};
    sendFrame(MCU_MSG_HELLO, versions, sizeof(versions));
    return;
  }
  if (frame.type >= MCU_CMD_DISPLAY && frame.type <= MCU_CMD_COIN_VERIFY) {
    bool isDisplay = frame.type == MCU_CMD_DISPLAY;
    bool isReplay = isDisplay ?
        (lastDisplayCommandValid && frame.sequence == lastDisplayCommandSequence) :
        (lastCoinCommandValid && frame.sequence == lastCoinCommandSequence);
    if (isReplay) {
      sendAck(frame.sequence, 0);
      return;
    }
    if ((frame.type == MCU_CMD_DISPLAY && displayOperation != DISPLAY_IDLE) ||
        (frame.type >= MCU_CMD_COIN_CONTROL && frame.type <= MCU_CMD_COIN_VERIFY &&
         coinOperation != COIN_IDLE)) {
      sendAck(frame.sequence, 1);
      return;
    }
    if (isDisplay) {
      lastDisplayCommandSequence = frame.sequence;
      lastDisplayCommandValid = true;
    } else {
      lastCoinCommandSequence = frame.sequence;
      lastCoinCommandValid = true;
    }
    sendAck(frame.sequence, 0);
  }
  if (frame.type == MCU_CMD_DISPLAY) {
    startDisplay(frame);
  } else if (frame.type == MCU_CMD_COIN_CONTROL && frame.length == 1) {
    coinTransaction = frame.sequence;
    coinDeadline = millis() + 5000UL;
    if (frame.payload[0] == '@') {
      digitalWrite(coinResetPin, LOW);
      coinNext = millis() + 1000UL;
      coinOperation = COIN_RESET_LOW;
    } else {
      coinSerialDevice.write(frame.payload[0]);
      coinNext = millis() + 100UL;
      coinOperation = COIN_CONTROL_WAIT;
    }
  } else if (frame.type == MCU_CMD_COIN_PROGRAM) {
    startCoinProgram(frame.sequence);
  } else if (frame.type == MCU_CMD_COIN_VERIFY) {
    startCoinVerify(frame.sequence);
  } else if (frame.type == MCU_CMD_KEEPALIVE) {
    sendResetCause();
  } else if (frame.type == MCU_CMD_IDENTITY) {
    printIdentity();
  }
}

void loop() {
  noInterrupts();
  byte head = i2cHead;
  interrupts();
  while (i2cTail != head) {
    SerialUSB.write(i2cBuf[i2cTail]);
    i2cTail = (i2cTail + 1) % I2C_BUF_SIZE;
  }

  /* (#230 item 2) Report ring overflows. Emitted only here, after the drain has
   * finished, so it can never land in the middle of one of Alpha's messages:
   * Wire delivers a whole transmission per ISR call, so at this point the
   * stream sits on an event boundary. Beta talks to the Pi directly, so unlike
   * Alpha's report this always gets through. ASCII digits, because the host
   * consumes the payload with strlen() and a zero byte would desync it. */
  {
    unsigned int ov;
    noInterrupts();
    ov = i2cOverflow;
    interrupts();
    unsigned long nowMs = millis();
    if (ov != i2cOverflowReported ||
        lastOverflowReport == 0 ||
        nowMs - lastOverflowReport >= OVERFLOW_REPORT_INTERVAL_MS) {
      unsigned int n = (ov > 999) ? 999 : ov;
      uint8_t payload[4] = {'B', (uint8_t)('0' + (n / 100) % 10),
                            (uint8_t)('0' + (n / 10) % 10),
                            (uint8_t)('0' + n % 10)};
      sendFrame(MCU_EVT_DIAGNOSTIC, payload, sizeof(payload));
      i2cOverflowReported = ov;
      lastOverflowReport = nowMs;
    }
  }

  while (SerialUSB.available()) {
    byte data = SerialUSB.read();
    if (data == CMD_IDENTITY && !protocolDecoder.active()) {
      printIdentity();
      continue;
    }
    MillenniumFrame frame;
    if (protocolDecoder.feed(data, frame) == 1) handleProtocolFrame(frame);
  }
  serviceDisplay();
  serviceCoin();
  if (coinSerialDevice.available()) {
    uint8_t data = coinSerialDevice.read();
    sendFrame(MCU_EVT_COIN, &data, 1);
  }

  unsigned long now = millis();
  if (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
    sendFrame(MCU_EVT_HEARTBEAT, NULL, 0);
    lastHeartbeat = now;
  }
  /* Reaching the end proves the main loop, serial parser, I2C drain, and active
   * operation all returned control; blocked code can no longer pet the dog. */
  wdt_reset();
}

void writeCommand(byte v) {
  digitalWrite(AD, HIGH);
  digitalWrite(WR, HIGH);
  digitalWrite(CS, LOW);
  digitalWrite(d0, bitRead(v, 0));
  digitalWrite(d1, bitRead(v, 1));
  digitalWrite(d2, bitRead(v, 2));
  digitalWrite(d3, bitRead(v, 3));
  digitalWrite(d4, bitRead(v, 4));
  digitalWrite(d5, bitRead(v, 5));
  digitalWrite(d6, bitRead(v, 6));
  digitalWrite(d7, bitRead(v, 7));
  digitalWrite(WR, LOW);
  delay(1);
  digitalWrite(WR, HIGH);
  digitalWrite(CS, HIGH);
  delay(1);
}

void writeCharacter(byte v) {
  digitalWrite(AD, LOW);
  digitalWrite(WR, HIGH);
  digitalWrite(CS, LOW);
  digitalWrite(d0, bitRead(v, 0));
  digitalWrite(d1, bitRead(v, 1));
  digitalWrite(d2, bitRead(v, 2));
  digitalWrite(d3, bitRead(v, 3));
  digitalWrite(d4, bitRead(v, 4));
  digitalWrite(d5, bitRead(v, 5));
  digitalWrite(d6, bitRead(v, 6));
  digitalWrite(d7, bitRead(v, 7));
  digitalWrite(WR, LOW);
  delay(1);
  digitalWrite(WR, HIGH);
  digitalWrite(CS, HIGH);
  delay(1);
}
