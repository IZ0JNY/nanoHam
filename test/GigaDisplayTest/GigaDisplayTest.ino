// Test GIGA Display Shield: barre colore, testo e touch
// Librerie: Arduino_GigaDisplay_GFX, Arduino_GigaDisplayTouch
#include "Arduino_GigaDisplay_GFX.h"
#include "Arduino_GigaDisplayTouch.h"

GigaDisplay_GFX display;
Arduino_GigaDisplayTouch touch;

#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define YELLOW  0xFFE0
#define CYAN    0x07FF
#define MAGENTA 0xF81F

void setup() {
  Serial.begin(115200);
  display.begin();          // 480x800, verticale

  const uint16_t colori[] = {RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, WHITE, BLACK};
  for (uint16_t c : colori) { display.fillScreen(c); delay(400); }

  int w = display.width(), h = display.height();
  for (int i = 0; i < 8; i++) display.fillRect(i * w / 8, 0, w / 8 + 1, h / 3, colori[i]);

  display.setTextColor(WHITE);
  display.setTextSize(4);
  display.setCursor(20, h / 3 + 30);
  display.print("GIGA R1 OK");
  display.setTextSize(2);
  display.setCursor(20, h / 3 + 90);
  display.print("nanoHam - test display");
  display.setCursor(20, h / 3 + 130);
  display.print("Tocca lo schermo...");

  if (touch.begin()) Serial.println("Touch OK");
  else               Serial.println("Touch NON inizializzato");
}

void loop() {
  GDTpoint_t p[5];
  uint8_t n = touch.getTouchPoints(p);
  for (uint8_t i = 0; i < n; i++) {
    display.fillCircle(p[i].x, p[i].y, 8, YELLOW);
    Serial.print("touch "); Serial.print(p[i].x); Serial.print(","); Serial.println(p[i].y);
  }
  delay(10);
}
