#include <SoftwareSerial.h>

// Gunakan pin 13 sebagai RX Arduino, pin 12 sebagai TX Arduino
SoftwareSerial SoftSerial(13, 12);

char c;

void setup() {
  Serial.begin(9600);
  SoftSerial.begin(9600);
  Serial.println("Arduino is ready");
}

void loop() {
  if (SoftSerial.available()) {
    c = SoftSerial.read();
    Serial.print(c);
  }

  if (Serial.available()) {
    c = Serial.read();
    SoftSerial.write(c);
  }
}
