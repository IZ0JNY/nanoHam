// nanoHam - GIGA R1 WiFi + GIGA Display Shield
// Riceve le misure dei 4 canali ADC via UART (fibra ottica) e le mostra a display.
//
// Collegamento: Serial1 della GIGA, D0 = RX1, D1 = TX1, 115200 8N1.
// Protocollo (una riga ASCII per misura, terminata da '\n'):
//   CH<n>,<valore>\n      es.  CH1,7.532
//   n = 1..4, valore gia' in unita' ingegneristiche (es. volt), punto decimale.
// Vedi PROTOCOLLO.md.

#include "Arduino_GigaDisplay_GFX.h"

// ---------- Configurazione ----------
#define LINK        Serial1
#define LINK_BAUD   115200
#define REFRESH_MS  100     // refresh display
#define TIMEOUT_MS  1000    // oltre questo tempo senza dati il canale va in "NO DATA"

struct Channel {
  const char *name;   // nome della misura
  const char *unit;   // unita' di misura
  float minV, maxV;   // fondo scala per la barra
  bool enabled;
  // stato runtime
  float value;
  unsigned long lastRx;
  bool fresh;
};

Channel ch[4] = {
  {"Tensione di prova", "V", 0.0, 10.0, true,  0, 0, false},
  {"Canale 2",          "V", 0.0, 10.0, false, 0, 0, false},
  {"Canale 3",          "V", 0.0, 10.0, false, 0, 0, false},
  {"Canale 4",          "V", 0.0, 10.0, false, 0, 0, false},
};

// ---------- Colori RGB565 ----------
#define BLACK   0x0000
#define WHITE   0xFFFF
#define GREY    0x7BEF
#define DKGREY  0x2104
#define GREEN   0x07E0
#define RED     0xF800
#define CYAN    0x07FF

GigaDisplay_GFX display;
const int W = 800, H = 480;         // orizzontale
const int ROW_H = H / 4;

// ---------- Ricezione UART ----------
char rxBuf[32];
uint8_t rxLen = 0;

void parseLine(char *s) {
  // atteso: CH<n>,<valore>
  if (s[0] != 'C' || s[1] != 'H') return;
  int n = s[2] - '0';
  if (n < 1 || n > 4 || s[3] != ',') return;
  char *end;
  float v = strtof(s + 4, &end);
  if (end == s + 4) return;           // nessun numero
  Channel &c = ch[n - 1];
  c.value = v;
  c.lastRx = millis();
  c.fresh = true;
}

void pollLink() {
  while (LINK.available()) {
    char c = LINK.read();
    if (c == '\r') continue;
    if (c == '\n') {
      rxBuf[rxLen] = 0;
      if (rxLen) parseLine(rxBuf);
      rxLen = 0;
    } else if (rxLen < sizeof(rxBuf) - 1) {
      rxBuf[rxLen++] = c;
    } else {
      rxLen = 0;                       // riga troppo lunga: scarta
    }
  }
}

// ---------- Display ----------
void drawStatic(int i) {
  int y = i * ROW_H;
  display.fillRect(0, y, W, ROW_H, BLACK);
  display.drawFastHLine(0, y + ROW_H - 1, W, DKGREY);
  display.setTextSize(3);
  display.setTextColor(ch[i].enabled ? CYAN : GREY, BLACK);
  display.setCursor(16, y + 12);
  display.print(ch[i].name);
  if (!ch[i].enabled) {
    display.setCursor(16, y + 56);
    display.setTextColor(GREY, BLACK);
    display.print("disabilitato");
  }
}

void drawValue(int i) {
  Channel &c = ch[i];
  int y = i * ROW_H;
  bool alive = c.lastRx && (millis() - c.lastRx < TIMEOUT_MS);

  // valore numerico
  char txt[16];
  if (alive) snprintf(txt, sizeof(txt), "%7.3f", c.value);
  else       snprintf(txt, sizeof(txt), "  -----");
  display.setTextSize(6);
  display.setTextColor(alive ? WHITE : RED, BLACK);
  display.setCursor(16, y + 50);
  display.print(txt);
  display.setTextSize(4);
  display.setCursor(16 + 7 * 36 + 12, y + 58);
  display.print(c.unit);

  // barra
  const int bx = 400, by = y + 56, bw = 380, bh = 40;
  float f = (c.value - c.minV) / (c.maxV - c.minV);
  if (f < 0) f = 0; if (f > 1) f = 1;
  int fill = alive ? (int)(f * (bw - 4)) : 0;
  display.drawRect(bx, by, bw, bh, GREY);
  display.fillRect(bx + 2, by + 2, fill, bh - 4, GREEN);
  display.fillRect(bx + 2 + fill, by + 2, bw - 4 - fill, bh - 4, BLACK);

  // stato link
  display.setTextSize(2);
  display.setCursor(W - 110, y + 14);
  display.setTextColor(alive ? GREEN : RED, BLACK);
  display.print(alive ? "  LINK" : "NODATA");
}

void setup() {
  Serial.begin(115200);
  LINK.begin(LINK_BAUD);
  display.begin();
  display.setRotation(1);              // 800x480 orizzontale
  display.fillScreen(BLACK);
  for (int i = 0; i < 4; i++) drawStatic(i);
}

void loop() {
  pollLink();

  static unsigned long lastRefresh = 0;
  if (millis() - lastRefresh >= REFRESH_MS) {
    lastRefresh = millis();
    for (int i = 0; i < 4; i++) {
      if (ch[i].enabled) drawValue(i);
      ch[i].fresh = false;
    }
  }
}
