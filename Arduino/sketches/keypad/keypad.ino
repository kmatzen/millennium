#include <Keypad.h>
#include <MagStripe.h>
#include <Wire.h>

const int hookUpPin = 5;
const int hookDownPin = 4;
const int hookCommonPin = 21;
volatile bool hookUpState = true;

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

  // SerialUSB.begin(115200);

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
  char *interchange;
  byte interchange_len;
  char *authProcessing;
  byte auth_processing_len;
  char *serviceAndPIN;
  byte service_and_pin_len;
};

struct MagstripeData parseTrack2(char *rawData, int length) {
  MagstripeData result;

  int startSentinelIndex = 0;
  while (rawData[startSentinelIndex] != ';') {
    ++startSentinelIndex;
  }
  int separatorIndex = startSentinelIndex;
  while (rawData[separatorIndex] != '=') {
    ++separatorIndex;
  }
  int endSentinelIndex = separatorIndex;
  while (rawData[endSentinelIndex] != '?') {
    ++endSentinelIndex;
  }

  if (startSentinelIndex != -1 && separatorIndex != -1 &&
      endSentinelIndex != -1) {
    result.pan = rawData + startSentinelIndex + 1;
    result.pan_len = separatorIndex - (startSentinelIndex + 1);

    const char *discretionaryData = rawData + separatorIndex + 1;
    if (endSentinelIndex - (separatorIndex + 1) >= 7) {
      result.expirationDate = discretionaryData;
      result.expiration_date_len = 4;
      result.serviceCode = discretionaryData + 4;
      result.service_code_len = 3;
      result.otherData = discretionaryData + 7;
      result.other_data_len = endSentinelIndex - (separatorIndex + 1) - 7;

#if 0
      // Interpret the service code
      if(result.serviceCode.length() == 3) {
        switch (result.serviceCode[0]) {
          case '1': result.interchange = "International interchange OK"; break;
          case '2': result.interchange = "International interchange, use IC where feasible"; break;
          case '3': result.interchange = "International interchange, use IC for mandatory transactions; magnetic stripe for fallback"; break;
          case '4': result.interchange = "National interchange only except under bilateral agreement"; break;
          case '5': result.interchange = "National interchange only except under bilateral agreement, use IC where feasible"; break;
          case '6': result.interchange = "National interchange, use IC for mandatory transactions; magnetic stripe for fallback"; break;
          case '7': result.interchange = "No interchange allowed (this is for private cards)"; break;
          default: result.interchange = "Unknown";
        }

        switch (result.serviceCode[1]) {
          case '0': result.authProcessing = "Normal"; break;
          case '2': result.authProcessing = "Contact issuer via online means"; break;
          case '4': result.authProcessing = "Contact issuer via online means except under bilateral agreement"; break;
          default: result.authProcessing = "Unknown";
        }

        switch (result.serviceCode[2]) {
          case '0': result.serviceAndPIN = "No restrictions and PIN required"; break;
          case '1': result.serviceAndPIN = "No restrictions"; break;
          case '2': result.serviceAndPIN = "Goods and services only (no cash)"; break;
          case '3': result.serviceAndPIN = "ATM only and PIN required"; break;
          case '4': result.serviceAndPIN = "Cash only"; break;
          case '5': result.serviceAndPIN = "Goods and services only (no cash) and PIN required"; break;
          case '6': result.serviceAndPIN = "No restrictions; use PIN where feasible"; break;
          case '7': result.serviceAndPIN = "Goods and services only (no cash); use PIN where feasible"; break;
          default: result.serviceAndPIN = "Unknown";
        }
      }
#endif
    }
  }

  return result;
}

void loop() {
#if 0
SerialUSB.write('>');
for (int i = 0; i < 24; ++i) {
SerialUSB.write('0' + (int)digitalRead(i));
}
SerialUSB.write('\r');
SerialUSB.write('\n');
return;
#endif
  char key = keypad.getKey();

  if (key != NO_KEY) {
    Wire.beginTransmission(0);
    Wire.write('K');
    Wire.write(key);
    Wire.endTransmission();
    // SerialUSB.write('K');
    // SerialUSB.write(key);
  }

  if (card.available()) {
    card.prime();
  }
  if (card.ready()) {
    char data[DATA_BUFFER_LEN];
    short chars = card.read(data, DATA_BUFFER_LEN);
    if (chars != -1) {
      MagstripeData parsedData = parseTrack2(data, chars);
      // SerialUSB.write('C');
      // SerialUSB.write(parsedData.pan, parsedData.pan_len);

      Wire.beginTransmission(0);
      Wire.write('C');
      Wire.write(parsedData.pan, parsedData.pan_len);
      Wire.endTransmission();
    }
  }

  pinMode(hookCommonPin, OUTPUT);
  digitalWrite(hookCommonPin, LOW);
  if (hookUpState) {
    bool hookDownNow = digitalRead(hookUpPin) && !digitalRead(hookDownPin);
    if (hookDownNow) {
      hookUpState = false;
      // SerialUSB.write("HD");
      Wire.beginTransmission(0);
      Wire.write("HD");
      Wire.endTransmission();
    }
  } else {
    bool hookUpNow = !digitalRead(hookUpPin) && digitalRead(hookDownPin);
    if (hookUpNow) {
      hookUpState = true;
      // SerialUSB.write("HU");
      Wire.beginTransmission(0);
      Wire.write("HU");
      Wire.endTransmission();
    }
  }
  digitalWrite(hookCommonPin, HIGH);
  pinMode(hookCommonPin, INPUT);
}
