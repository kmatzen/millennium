#include <SoftwareSerial.h>
#include <Wire.h>

#define d0 5     // Pin 15 on VFD - green
#define d1 6     // Pin 13 on VFD - yellow
#define d2 7     // Pin 11 on VFD - orange
#define d3 8     // Pin 9 on VFD - red
#define d4 9     // Pin 7 on VFD - brown
#define d5 10    // Pin 5 on VFD - black
#define d6 11    // Pin 3 on VFD - white
#define d7 12    // Pin 1 on VFD - gray
#define WR 4     // Pin 17 on VFD - blue
#define AD 0     // Pin 19 on VFD - violet
#define RD 1     // Pin 21 on VFD - gray
#define CS 17    // Pin 23 on VFD - white
#define TEST 16  // Pin 25 on VFD - black
#define RESET 13 // Pin 20 on VFD - yellow

const int coinResetPin = 15; // Define the reset pin
SoftwareSerial coinSerialDevice(14,
                                23); // RX, TX pins for the serial communication

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
int scrollDelay = 10;

void setup() {
  SerialUSB.begin(9600); // 115200);

  Wire.begin(0);
  Wire.onReceive(receiveEvent);

  pinMode(coinResetPin, OUTPUT);
  pinMode(14, INPUT);
  digitalWrite(coinResetPin, HIGH); // Set the reset pin to inactive high
  coinSerialDevice.begin(600);      // Initialize the SoftwareSerial at 600 baud

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
  // pinMode(12,OUTPUT);

  // Default States
  // digitalWrite(12, HIGH);
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
  // According to an anonymous source, this needs to be done once and a while
  // because of corrupt characters appearing on the screen. We have confirmed
  // this. The reset is very fast, you can shorten the delays a bit, but you
  // will have to play around to see what works for you.
  digitalWrite(RESET, HIGH);
  delay(2);
  digitalWrite(RESET, LOW);
  delay(10);
}

void vfdtest() {
  // This will kick off the VFD internal test. Basically just steps through all
  // the characters built in to the display. Procedure is, hold x5 (VFD Display
  // pin 25) low for 100ms during a reset. Reset does not start until reset (VFD
  // Display pin 20) is low again.
  digitalWrite(TEST, LOW);
  digitalWrite(RESET, HIGH);
  delay(50); // This can be shorter, but best results showed this was good.
  digitalWrite(RESET, LOW);
  delay(100);   // NO TOUCHY!
  delay(15000); // This counter is running while the test sequence is running.
                // Make this longer if you like, the display will keep looping
                // the test until you restart
  digitalWrite(RESET, HIGH);
  delay(10);
  digitalWrite(RESET, LOW);
  digitalWrite(TEST, HIGH);
}

void loop() {
  byte buf[100];
  if (SerialUSB.available()) {
    byte data = SerialUSB.read();
    if (data == 2) {
      while (!SerialUSB.available())
        ;
      byte num_bytes = SerialUSB.read();
      for (int i = 0; i < num_bytes; ++i) {
        while (!SerialUSB.available())
          ;
        buf[i] = SerialUSB.read();
      }
      delay(100);
      writeCommand(0);
      for (int i = 0; i < num_bytes; ++i) {
        writeCharacter(buf[i]);
      }
    } else if (data == 3) {
      while (!SerialUSB.available())
        ;
      char data = SerialUSB.read();

      if (data == '@') {
        digitalWrite(coinResetPin, LOW);
        delay(1000); // Wait for a moment with reset line LOW
        digitalWrite(coinResetPin, HIGH);
        delay(1000); // Delay for a second after resetting (adjust as needed)
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
        while (!coinSerialDevice.available())
          ;
        byte val = coinSerialDevice.read();
        if (val != coinEeprom[i]) {
          SerialUSB.write('E');
          SerialUSB.write(lowByte(i));
          SerialUSB.write(val);
          SerialUSB.write(coinEeprom[i]);
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
  digitalWrite(WR, HIGH); // Prepare to write
  digitalWrite(CS, LOW);
  digitalWrite(d0, bitRead(v, 0));
  digitalWrite(d1, bitRead(v, 1));
  digitalWrite(d2, bitRead(v, 2));
  digitalWrite(d3, bitRead(v, 3));
  digitalWrite(d4, bitRead(v, 4));
  digitalWrite(d5, bitRead(v, 5));
  digitalWrite(d6, bitRead(v, 6));
  digitalWrite(d7, bitRead(v, 7));
  digitalWrite(WR, LOW); // Write Complete
  delay(1);
  digitalWrite(WR, HIGH);
  digitalWrite(CS, HIGH);
  delay(1);
  // delay(scrollDelay);
}

void writeCharacter(byte v) {
  digitalWrite(AD, LOW);
  digitalWrite(WR, HIGH); // Prepare to write
  digitalWrite(CS, LOW);
  digitalWrite(d0, bitRead(v, 0));
  digitalWrite(d1, bitRead(v, 1));
  digitalWrite(d2, bitRead(v, 2));
  digitalWrite(d3, bitRead(v, 3));
  digitalWrite(d4, bitRead(v, 4));
  digitalWrite(d5, bitRead(v, 5));
  digitalWrite(d6, bitRead(v, 6));
  digitalWrite(d7, bitRead(v, 7));
  digitalWrite(WR, LOW); // Write Complete
  delay(1);
  digitalWrite(WR, HIGH);
  digitalWrite(CS, HIGH);
  delay(1);
  // delay(scrollDelay);
}
