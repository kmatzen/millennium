#include <Keypad.h>
#include <MagStripe.h>
#include <Wire.h>

#define I2C_DISPLAY_ADDR 8

const int hookUpPin = 5;
const int hookDownPin = 4;
const int hookCommonPin = 21;

bool hookUpState = true;
unsigned long lastHookChange = 0;
const unsigned long DEBOUNCE_MS = 50;

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

void setup() {
  delay(2000);

  card.begin(2);

  Wire.begin();

  pinMode(hookUpPin, INPUT_PULLUP);
  pinMode(hookDownPin, INPUT_PULLUP);
  pinMode(hookCommonPin, OUTPUT);
  digitalWrite(hookCommonPin, HIGH);
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
  result.valid = false;
  result.pan = NULL;
  result.pan_len = 0;

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

void loop() {
  char key = keypad.getKey();

  if (key != NO_KEY) {
    Wire.beginTransmission(I2C_DISPLAY_ADDR);
    Wire.write('K');
    Wire.write(key);
    Wire.endTransmission();
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
        Wire.beginTransmission(I2C_DISPLAY_ADDR);
        Wire.write('C');
        Wire.write(parsedData.pan, parsedData.pan_len);
        Wire.endTransmission();
      }
    }
  }

  unsigned long now = millis();
  pinMode(hookCommonPin, OUTPUT);
  digitalWrite(hookCommonPin, LOW);
  if (now - lastHookChange >= DEBOUNCE_MS) {
    if (hookUpState) {
      bool hookDownNow = digitalRead(hookUpPin) && !digitalRead(hookDownPin);
      if (hookDownNow) {
        hookUpState = false;
        lastHookChange = now;
        Wire.beginTransmission(I2C_DISPLAY_ADDR);
        Wire.write("HD");
        Wire.endTransmission();
      }
    } else {
      bool hookUpNow = !digitalRead(hookUpPin) && digitalRead(hookDownPin);
      if (hookUpNow) {
        hookUpState = true;
        lastHookChange = now;
        Wire.beginTransmission(I2C_DISPLAY_ADDR);
        Wire.write("HU");
        Wire.endTransmission();
      }
    }
  }
  digitalWrite(hookCommonPin, HIGH);
  pinMode(hookCommonPin, INPUT);
}
