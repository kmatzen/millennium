#include <SoftwareSerial.h>
#include <Wire.h>

#define I2C_DISPLAY_ADDR 8

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

const unsigned long SERIAL_TIMEOUT_MS = 2000;

static bool waitForSerial() {
  unsigned long start = millis();
  while (!SerialUSB.available()) {
    if (millis() - start > SERIAL_TIMEOUT_MS) return false;
  }
  return true;
}

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
}

void receiveEvent(int howMany) {
  while (Wire.available()) {
    SerialUSB.write(Wire.read());
  }
}

void vfdreset() {
  digitalWrite(RESET, HIGH);
  delay(2);
  digitalWrite(RESET, LOW);
  delay(10);
}

void loop() {
  byte buf[100];
  if (SerialUSB.available()) {
    byte data = SerialUSB.read();
    if (data == 2) {
      if (!waitForSerial()) return;
      byte num_bytes = SerialUSB.read();
      for (int i = 0; i < num_bytes; ++i) {
        if (!waitForSerial()) return;
        buf[i] = SerialUSB.read();
      }
      delay(100);
      writeCommand(0);
      for (int i = 0; i < num_bytes; ++i) {
        writeCharacter(buf[i]);
      }
    } else if (data == 3) {
      if (!waitForSerial()) return;
      char data = SerialUSB.read();

      if (data == '@') {
        digitalWrite(coinResetPin, LOW);
        delay(1000);
        digitalWrite(coinResetPin, HIGH);
        delay(1000);
      } else {
        coinSerialDevice.write(data);
        delay(100);
      }
    } else if (data == 4) {
      SerialUSB.write("A");
      for (int i = 0; i < 256; ++i) {
        coinSerialDevice.write('E');
        delay(20);
        coinSerialDevice.write('A');
        delay(20);
        coinSerialDevice.write('P');
        delay(20);
        coinSerialDevice.write('w');
        delay(20);
        coinSerialDevice.write(lowByte(i));
        delay(20);
        coinSerialDevice.write(coinEeprom[i]);
        delay(20);
      }
      SerialUSB.write('B');
    } else if (data == 5) {
      SerialUSB.write('D');
      for (int i = 0; i < 256; ++i) {
        while (coinSerialDevice.available()) {
          coinSerialDevice.read();
        }
        coinSerialDevice.write('q');
        delay(20);
        coinSerialDevice.write(0x01);
        delay(20);
        coinSerialDevice.write(lowByte(i));
        delay(20);
        unsigned long start = millis();
        while (!coinSerialDevice.available()) {
          if (millis() - start > SERIAL_TIMEOUT_MS) break;
        }
        if (coinSerialDevice.available()) {
          byte val = coinSerialDevice.read();
          if (val != coinEeprom[i]) {
            SerialUSB.write('E');
            SerialUSB.write(lowByte(i));
            SerialUSB.write(val);
            SerialUSB.write(coinEeprom[i]);
          }
        }
      }
      SerialUSB.write('F');
    }
  }
  if (coinSerialDevice.available()) {
    char data = coinSerialDevice.read();
    SerialUSB.write("V");
    SerialUSB.write(data);
  }
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
