// I2CScan.ino - scansione temporanea di Wire e Wire1 (UNO R4 WiFi)
#include <Wire.h>

void scanBus(TwoWire &bus, const char *name) {
  uint8_t n = 0;
  Serial.print(name);
  Serial.print(":");
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    bus.beginTransmission(a);
    if (bus.endTransmission() == 0) {
      Serial.print(" 0x");
      if (a < 16) Serial.print('0');
      Serial.print(a, HEX);
      n++;
    }
  }
  if (n == 0) Serial.print(" nessun dispositivo");
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(100000);
  Wire1.begin();
  Wire1.setClock(100000);
}

void loop() {
  scanBus(Wire, "Wire (SDA/SCL)");
  scanBus(Wire1, "Wire1 (Qwiic)");
  delay(2000);
}
