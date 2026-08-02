/*
 * Millennium Alpha (keypad) firmware.
 * Build with FQBN arduino:avr:millennium_alpha so the board identifies as
 * "Millennium Alpha" on USB (/dev/serial/by-id/usb-Arduino_LLC_Millennium_Alpha-if00).
 */
#include <Keypad.h>
#include <MagStripe.h>
#include <Wire.h>
#include <avr/wdt.h>

#define I2C_DISPLAY_ADDR 8

/* I2C event prefixes sent to display Arduino */
#define EVT_KEY        'K'
#define EVT_HOOK_UP    "HU"
#define EVT_HOOK_DOWN  "HD"
#define EVT_CARD       'C'
#define EVT_DIAG       'G'   /* (#230) diagnostics: 'G' + 'A' + 3 ASCII digits.
                                * Not 'X' -- display.ino uses that for a serial
                                * timeout, and a marker collision makes the host
                                * eat the events behind it (#259). */

const int hookUpPin = 5;
const int hookDownPin = 4;
const int hookCommonPin = 21;

bool hookUpState = true;
unsigned long lastHookChange = 0;
const unsigned long DEBOUNCE_MS = 50;

/* (#230) Retry budget for an I2C message to Beta.  Deliberately short: it
 * comfortably covers a VFD repaint (~180 ms, display.ino:199-207) but NOT the
 * ~2 s coin-validator reset (display.ino:213-216).  Blocking the keypad scan
 * for two seconds would lose more keypresses than it saved, and the watchdog
 * is only 4 s. */
const uint8_t I2C_SEND_ATTEMPTS = 8;
const unsigned long I2C_RETRY_DELAY_MS = 25;

/* Messages Beta never acknowledged, even after the retries above, and the last
 * value we managed to report to the host. */
unsigned int i2cDropped = 0;
unsigned int i2cDroppedReported = 0;
unsigned long lastDropReport = 0;

/* Re-announce the drop count this often, zero included.
 *
 * Reporting only on change loses the news entirely in the case that matters
 * most: drops happen when the link to Beta is sick, which is exactly when the
 * Pi is likely to be mid-reconnect and not reading the port. Beta forwards to
 * a USB endpoint that discards when no host is attached, so a one-shot report
 * simply evaporates -- observed doing exactly that.
 *
 * Re-announcing zero matters too: without it, a gauge left at 3 would stay at
 * 3 after Alpha reboots with a clean counter. It also gives the Pi its only
 * positive sign of life from Alpha -- Beta's heartbeat says nothing about
 * whether the keypad board is still running. 5 bytes every 30 s. */
const unsigned long DROP_REPORT_INTERVAL_MS = 30000;

/* (#232) What the hook read as during setup(), kept for the status query.
 * Printing it at boot would be useless: an external reset re-enumerates the
 * USB CDC port, so anything written before a host attaches is discarded. */
bool bootHookUp = false;
bool bootHookReported = false;

const byte ROWS = 4;
const byte COLS = 7;
char keys[ROWS][COLS] = {{'1', '2', '3', 'A', 'B', 'C', 'D'},
                         {'4', '5', '6', 'E', 'F', 'G', 'H'},
                         {'7', '8', '9', 'I', 'J', 'K', 'L'},
                         {'*', '0', '#', 'M', 'N', 'O', 'P'}};

byte rowPins[ROWS] = {9, 8, 7, 6};
byte colPins[COLS] = {10, 11, 12, 13, 18, 19, 20};
static const byte DATA_BUFFER_LEN = 108;

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

MagStripe card(22, 0, 1);

/*
 * (#230) Send one I2C message to Beta, retrying while it NACKs.
 *
 * Wire.endTransmission() returns 0 only when Beta acknowledged every byte; 2
 * and 3 are NACKs, which is exactly what happens while Beta sits in a delay()
 * or repaints the VFD.  Every call site used to discard that status, so a
 * keypress lost to a busy Beta was indistinguishable from one delivered --
 * which is why "every keypress eventually reaches the Pi" did not hold.
 *
 * Returns true if the message landed.
 */
bool i2cTrySend(const uint8_t *payload, uint8_t len) {
  for (uint8_t attempt = 0; attempt < I2C_SEND_ATTEMPTS; attempt++) {
    Wire.beginTransmission(I2C_DISPLAY_ADDR);
    Wire.write(payload, len);
    if (Wire.endTransmission() == 0) {
      return true;
    }
    wdt_reset();
    delay(I2C_RETRY_DELAY_MS);
  }
  return false;
}

/* As above, but tallies a failure. Use this for real phone events; the drop
 * report itself uses i2cTrySend so a failed report cannot inflate the very
 * number it is trying to report. */
bool i2cSend(const uint8_t *payload, uint8_t len) {
  if (i2cTrySend(payload, len)) return true;
  i2cDropped++;
  return false;
}

/*
 * (#230 item 2) Tell the host how many messages we lost.
 *
 * This necessarily rides the same I2C link that dropped them, so it only gets
 * through once the link is healthy again. That is fine: the count is
 * cumulative and the host publishes it as a gauge, so a late report is still
 * correct. Encoded as ASCII digits because the host consumes the payload with
 * strlen(), and a raw zero byte would truncate the event and desync the stream.
 */
void reportDropsIfChanged() {
  uint8_t msg[5];
  unsigned int n;
  unsigned long now = millis();
  bool changed = (i2cDropped != i2cDroppedReported);
  bool due = (lastDropReport == 0) ||
             (now - lastDropReport >= DROP_REPORT_INTERVAL_MS);

  if (!changed && !due) return;

  n = (i2cDropped > 999) ? 999 : i2cDropped;
  msg[0] = EVT_DIAG;
  msg[1] = 'A';
  msg[2] = '0' + (n / 100) % 10;
  msg[3] = '0' + (n / 10) % 10;
  msg[4] = '0' + n % 10;

  if (i2cTrySend(msg, sizeof(msg))) {
    i2cDroppedReported = i2cDropped;
    lastDropReport = now;
  }
}

/*
 * Sample the hook switch.  hookCommonPin is driven low only for the read, the
 * same way loop() does it, so the two pull-ups can be told apart.
 */
bool readHookUp() {
  bool up;
  digitalWrite(hookCommonPin, LOW);
  delayMicroseconds(50);   /* let the pull-ups settle before sampling */
  up = !digitalRead(hookUpPin) && digitalRead(hookDownPin);
  digitalWrite(hookCommonPin, HIGH);
  return up;
}

void setup() {
  delay(2000);

  Serial.begin(9600);   /* diagnostics only; nothing on the Pi reads this port */

  card.begin(2);

  Wire.begin();

  pinMode(hookUpPin, INPUT_PULLUP);
  pinMode(hookDownPin, INPUT_PULLUP);
  pinMode(hookCommonPin, OUTPUT);
  digitalWrite(hookCommonPin, HIGH);

  /* (#232) Sample the hook once and report it, so the firmware and the Pi
   * start out agreeing.  Previously hookUpState was simply assumed true and
   * HU/HD were only ever emitted on a transition, so a phone that booted with
   * the handset off the hook had the firmware believing it was up and the
   * daemon believing it was down, with nothing to reconcile them until the
   * handset next moved.
   *
   * An ambiguous reading (both pins alike, i.e. mid-travel) is treated as
   * down, which is what daemon_state.c already defaults to -- agreeing on the
   * safe state beats agreeing on nothing.
   *
   * Beta may still be inside its own setup() when we first try, so this leans
   * on i2cSend()'s retry (#230); a bare fire-and-forget write would vanish.
   * The extra outer attempts are affordable here because the keypad scan has
   * not started yet.
   */
  hookUpState = readHookUp();
  bootHookUp = hookUpState;
  bootHookReported = false;
  lastHookChange = millis();
  {
    const uint8_t *msg = (const uint8_t *)(hookUpState ? EVT_HOOK_UP : EVT_HOOK_DOWN);
    uint8_t attempt;
    for (attempt = 0; attempt < 3; attempt++) {
      if (i2cSend(msg, 2)) { bootHookReported = true; break; }
      delay(100);
    }
  }

  wdt_enable(WDTO_4S);
}

struct MagstripeData {
  char *pan;
  byte pan_len;
  char *expirationDate;
  byte expiration_date_len;
  char *serviceCode;
  byte service_code_len;
  char *otherData;
  byte other_data_len;
  bool valid;
};

/*
 * Parse ISO/IEC 7813 Track 2 data.
 * Format: ;PAN=YYMMSSSDDDDDDDDDDDDDD?LRC
 * Returns a struct with pointers into rawData (valid only while rawData is alive).
 */
struct MagstripeData parseTrack2(char *rawData, int length) {
  MagstripeData result;
  /* Initialize every field so all return paths yield a fully-defined struct.
   * The discretionary fields below are only filled in when a long-enough
   * discretionary section is present; without these defaults the early returns
   * (and the short-data case) would return uninitialized pointers. */
  result.valid = false;
  result.pan = NULL;
  result.pan_len = 0;
  result.expirationDate = NULL;
  result.expiration_date_len = 0;
  result.serviceCode = NULL;
  result.service_code_len = 0;
  result.otherData = NULL;
  result.other_data_len = 0;

  int startSentinelIndex = -1;
  for (int i = 0; i < length; i++) {
    if (rawData[i] == ';') {
      startSentinelIndex = i;
      break;
    }
  }
  if (startSentinelIndex < 0) return result;

  int separatorIndex = -1;
  for (int i = startSentinelIndex + 1; i < length; i++) {
    if (rawData[i] == '=') {
      separatorIndex = i;
      break;
    }
  }
  if (separatorIndex < 0) return result;

  int endSentinelIndex = -1;
  for (int i = separatorIndex + 1; i < length; i++) {
    if (rawData[i] == '?') {
      endSentinelIndex = i;
      break;
    }
  }
  if (endSentinelIndex < 0) return result;

  result.valid = true;
  result.pan = rawData + startSentinelIndex + 1;
  result.pan_len = separatorIndex - (startSentinelIndex + 1);

  int discretionaryLen = endSentinelIndex - (separatorIndex + 1);
  if (discretionaryLen >= 7) {
    const char *disc = rawData + separatorIndex + 1;
    result.expirationDate = (char *)disc;
    result.expiration_date_len = 4;
    result.serviceCode = (char *)(disc + 4);
    result.service_code_len = 3;
    result.otherData = (char *)(disc + 7);
    result.other_data_len = discretionaryLen - 7;
  }

  return result;
}

/*
 * Status query on Alpha's own USB serial, which nothing else uses -- the daemon
 * only opens Beta. Send any byte to that port and the firmware reports what it
 * sampled at boot, whether Beta took the report, the live hook state, and the
 * running I2C drop count.
 *
 * This is how the #232 boot sample is observable at all: the value cannot be
 * printed as it happens, because the reset that produces it also re-enumerates
 * the port.
 */
void serviceStatusQuery() {
  if (!Serial.available()) return;
  while (Serial.available()) Serial.read();

  Serial.print(F("boot_hook="));
  Serial.print(bootHookUp ? F("UP") : F("DOWN"));
  Serial.print(F(" boot_reported="));
  Serial.print(bootHookReported ? F("yes") : F("no"));
  Serial.print(F(" hook="));
  Serial.print(hookUpState ? F("UP") : F("DOWN"));
  Serial.print(F(" i2c_drops="));
  Serial.print(i2cDropped);
  Serial.print(F(" reported="));
  Serial.println(i2cDroppedReported);
}

void loop() {
  wdt_reset();

  serviceStatusQuery();
  reportDropsIfChanged();

  char key = keypad.getKey();

  if (key != NO_KEY) {
    uint8_t msg[2];
    msg[0] = EVT_KEY;
    msg[1] = (uint8_t)key;
    i2cSend(msg, sizeof(msg));
  }

  if (card.available()) {
    card.prime();
  }
  if (card.ready()) {
    char data[DATA_BUFFER_LEN];
    short chars = card.read(data, DATA_BUFFER_LEN);
    if (chars > 0) {
      MagstripeData parsedData = parseTrack2(data, chars);
      if (parsedData.valid && parsedData.pan_len > 0) {
        /* Wire's TX buffer is 32 bytes on AVR, and a longer write is silently
         * truncated -- clamp explicitly so the limit is visible here rather
         * than being discovered as a mangled PAN. */
        uint8_t pan_len = parsedData.pan_len;
        uint8_t msg[1 + 31];
        if (pan_len > sizeof(msg) - 1) pan_len = sizeof(msg) - 1;
        msg[0] = EVT_CARD;
        memcpy(msg + 1, parsedData.pan, pan_len);
        i2cSend(msg, 1 + pan_len);
      }
    }
  }

  unsigned long now = millis();
  digitalWrite(hookCommonPin, LOW);
  if (now - lastHookChange >= DEBOUNCE_MS) {
    if (hookUpState) {
      bool hookDownNow = digitalRead(hookUpPin) && !digitalRead(hookDownPin);
      if (hookDownNow) {
        hookUpState = false;
        lastHookChange = now;
        i2cSend((const uint8_t *)EVT_HOOK_DOWN, 2);
      }
    } else {
      bool hookUpNow = !digitalRead(hookUpPin) && digitalRead(hookDownPin);
      if (hookUpNow) {
        hookUpState = true;
        lastHookChange = now;
        i2cSend((const uint8_t *)EVT_HOOK_UP, 2);
      }
    }
  }
  digitalWrite(hookCommonPin, HIGH);
}
