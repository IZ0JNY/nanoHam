// nanoHam - GIGA R1 WiFi + GIGA Display Shield
// Sviluppatore: IZ0JNY
// Riceve le misure dei 4 canali ADC via UART (fibra ottica) e le mostra a display.
//
// Collegamento: Serial1 della GIGA, D0 = RX1, D1 = TX1, 115200 8N1.
// Protocollo (una riga ASCII per misura, terminata da '\n'):
//   CH<n>,<valore>\n      es.  CH1,7.532
//   n = 1..4, valore = tensione in ingresso 0..10 V, punto decimale.
//   Il rapporto di scala (partitore) si applica qui, nella tabella ch[].
// Vedi PROTOCOLLO.md.

#include "Arduino_GigaDisplay_GFX.h"

// ---------- Configurazione ----------
#define LINK        Serial1
#define LINK_BAUD   115200
#define REFRESH_MS  100     // refresh display
#define TIMEOUT_MS  1000    // oltre questo tempo senza dati il canale va in "NO DATA"

// Ingresso fisico sempre 0..10 V. Valore mostrato = Vin * scale + offset.
// Es. partitore 10:1 davanti a 0..100 V  -> scale = 10.0, unit = "V".
#define VIN_FS 10.0f

struct Channel {
  const char *name;   // nome della misura
  const char *unit;   // unita' di misura mostrata
  float scale;        // rapporto di scala (partitore, sensore, ...)
  float offset;       // offset in unita' mostrate
  uint8_t decimals;   // decimali mostrati
  bool enabled;
  // stato runtime
  float value;        // valore gia' scalato
  unsigned long lastRx;
  int lastSegs;       // segmenti accesi all'ultimo refresh
  int peakSeg;        // segmento di picco
  unsigned long peakTime;
};

Channel ch[4] = {
  {"Tensione di prova", "V", 1.0, 0.0, 3, true,  0, 0, -1, -1, 0},
  {"Canale 2",          "V", 1.0, 0.0, 3, false, 0, 0, -1, -1, 0},
  {"Canale 3",          "V", 1.0, 0.0, 3, false, 0, 0, -1, -1, 0},
  {"Canale 4",          "V", 1.0, 0.0, 3, false, 0, 0, -1, -1, 0},
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
  c.value = v * c.scale + c.offset;   // v = tensione in ingresso 0..10 V
  c.lastRx = millis();
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
#define SEGS      40          // segmenti della barra
#define PEAK_MS   2000        // tempo di mantenimento del picco

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// colore lungo la scala: verde -> giallo -> arancio -> rosso
uint16_t levelColor(float f) {
  f = constrain(f, 0.0f, 1.0f);
  if (f < 0.5f) return rgb((uint8_t)(f * 2 * 255), 220, 40);
  return rgb(255, (uint8_t)((1 - f) * 2 * 220), 40);
}

// colore spento del segmento (stessa tinta, molto scura)
uint16_t dimColor(float f) {
  if (f < 0.5f) return rgb((uint8_t)(f * 2 * 50), 45, 10);
  return rgb(55, (uint8_t)((1 - f) * 2 * 45), 10);
}

float chMin(const Channel &c) { return c.offset; }
float chMax(const Channel &c) { return VIN_FS * c.scale + c.offset; }

const int BX = 16, BW = W - 32, BH = 30;      // barra
const int SEG_W = BW / SEGS;

void drawSeg(int i, int s, bool on, bool peak) {
  int y = i * ROW_H + ROW_H - BH - 10;
  float f = (s + 0.5f) / SEGS;
  uint16_t col = peak ? WHITE : (on ? levelColor(f) : dimColor(f));
  display.fillRect(BX + s * SEG_W, y, SEG_W - 3, BH, col);
}

void drawStatic(int i) {
  Channel &c = ch[i];
  int y = i * ROW_H;
  display.fillRect(0, y, W, ROW_H, BLACK);
  display.fillRect(0, y + 6, 6, ROW_H - 12, c.enabled ? CYAN : DKGREY);  // accento
  display.drawFastHLine(0, y + ROW_H - 1, W, DKGREY);
  display.setTextSize(2);
  display.setTextColor(c.enabled ? CYAN : GREY, BLACK);
  display.setCursor(BX, y + 10);
  display.print(c.name);
  if (!c.enabled) {
    display.setCursor(BX, y + 40);
    display.print("disabilitato");
    return;
  }
  // fondo scala
  char txt[24];
  display.setTextColor(GREY, BLACK);
  snprintf(txt, sizeof(txt), "%.*f", c.decimals > 1 ? 1 : c.decimals, chMin(c));
  display.setCursor(BX, y + 30);
  display.print(txt);
  snprintf(txt, sizeof(txt), "%.*f %s", c.decimals > 1 ? 1 : c.decimals, chMax(c), c.unit);
  display.setCursor(W - 16 - strlen(txt) * 12, y + 30);
  display.print(txt);
  for (int s = 0; s < SEGS; s++) drawSeg(i, s, false, false);
  c.lastSegs = 0;
  c.peakSeg = -1;
}

void drawValue(int i) {
  Channel &c = ch[i];
  int y = i * ROW_H;
  bool alive = c.lastRx && (millis() - c.lastRx < TIMEOUT_MS);
  float f = (c.value - chMin(c)) / (chMax(c) - chMin(c));
  f = constrain(f, 0.0f, 1.0f);

  // valore numerico grande, colorato come il livello
  char txt[16];
  if (alive) snprintf(txt, sizeof(txt), "%8.*f", c.decimals, c.value);
  else       snprintf(txt, sizeof(txt), "   -----");
  display.setTextSize(5);
  display.setTextColor(alive ? levelColor(f) : RED, BLACK);
  int vx = 300;
  display.setCursor(vx, y + 8);
  display.print(txt);
  display.setTextSize(3);
  display.setTextColor(WHITE, BLACK);
  display.setCursor(vx + 8 * 30 + 10, y + 20);
  display.print(c.unit);

  // stato link
  display.setTextSize(2);
  display.setCursor(W - 90, y + 10);
  display.setTextColor(alive ? GREEN : RED, BLACK);
  display.print(alive ? "  LINK" : "NODATA");

  // barra a segmenti: ridisegna solo quelli cambiati
  int segs = alive ? (int)(f * SEGS + 0.5f) : 0;
  if (segs > c.lastSegs) for (int s = c.lastSegs; s < segs; s++) drawSeg(i, s, true, false);
  else                   for (int s = segs; s < c.lastSegs; s++) drawSeg(i, s, false, false);
  c.lastSegs = segs;

  // picco con mantenimento
  int pk = segs - 1;
  unsigned long now = millis();
  if (pk > c.peakSeg || now - c.peakTime > PEAK_MS) {
    if (c.peakSeg >= 0) drawSeg(i, c.peakSeg, c.peakSeg < segs, false);
    c.peakSeg = pk;
    c.peakTime = now;
  }
  if (c.peakSeg >= 0) drawSeg(i, c.peakSeg, true, true);
}

void splash() {
  display.fillScreen(BLACK);
  display.setTextColor(CYAN, BLACK);
  display.setTextSize(8);
  display.setCursor((W - 7 * 48) / 2, 140);
  display.print("nanoHam");
  display.setTextColor(WHITE, BLACK);
  display.setTextSize(3);
  const char *dev = "Sviluppatore: IZ0JNY";
  display.setCursor((W - strlen(dev) * 18) / 2, 260);
  display.print(dev);
  for (int s = 0; s < SEGS; s++)                     // barra colorata decorativa
    display.fillRect(BX + s * SEG_W, 330, SEG_W - 3, 20, levelColor((s + 0.5f) / SEGS));
  delay(2000);
}

void setup() {
  Serial.begin(115200);
  LINK.begin(LINK_BAUD);
  display.begin();
  display.setRotation(1);              // 800x480 orizzontale
  splash();
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
    }
  }
}
