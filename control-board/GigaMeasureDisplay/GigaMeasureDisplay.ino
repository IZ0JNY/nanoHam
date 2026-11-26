// nanoHam - GIGA R1 WiFi + GIGA Display Shield
// Sviluppatore: IZ0JNY
// Riceve le misure dei 4 canali ADC via UART (fibra ottica) e le mostra a display.
//
// Collegamento: Serial1 della GIGA, D0 = RX0, D1 = TX0 (serigrafia RX0/TX0), 115200 8N1.
// Protocollo (una riga ASCII per misura, terminata da '\n'):
//   CH<n>,<valore>\n      es.  CH1,7.532
//   n = 1..4, valore = tensione in ingresso 0..10 V, punto decimale.
//   Il rapporto di scala (partitore) si applica qui, nella tabella ch[].
// Vedi PROTOCOLLO.md.
//
// Grafica: pannello grigio a sinistra (titolo, stato, 4 canali con barra),
// pannello teal a destra (indicatore ad arco del CH1 + simbolo alta tensione).

#include "Arduino_GigaDisplay_GFX.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

// ---------- Configurazione ----------
#define LINK        Serial1
#define LINK_BAUD   115200
#define REFRESH_MS  100     // refresh display
#define TIMEOUT_MS  1000    // oltre questo tempo senza dati il canale va in "NO DATA"
#define DEMO        1       // 1 = senza dati dalla UART genera un'onda 0..10 V di prova
#define SELFTEST    0       // 1 = la GIGA trasmette "CH1,5.000" su Serial1 ogni 100 ms (test in loopback)
#define DEBUG_RX    0       // 1 = stampa ogni riga ricevuta sulla seriale USB
#define METER_CH    0       // canale mostrato sul voltmetro analogico (0 = CH1)

// Ingresso fisico sempre 0..10 V. Valore mostrato = Vin * scale + offset.
// CH1: 0..10 V sull'ADC = 0..10 kV reali -> scale = 1.0, unit = "kV".
#define VIN_FS 10.0f

struct Channel {
  const char *name;   // nome della misura
  const char *unit;   // unita' di misura mostrata
  float scale;        // rapporto di scala (partitore, sensore, ...)
  float offset;       // offset in unita' mostrate
  uint8_t decimals;   // decimali mostrati
  bool enabled;
  // stato runtime
  float vin;          // tensione in ingresso 0..10 V
  float value;        // valore gia' scalato
  unsigned long lastRx;
  int lastBar;        // larghezza barra all'ultimo refresh
  char lastTxt[16];   // testo mostrato all'ultimo refresh
};

Channel ch[4] = {
  {"V Helix",       "kV", 1.0, 0.0, 3, true,  0, 0, 0, -1, ""},
  {"Canale 2",      "V",  1.0, 0.0, 2, false, 0, 0, 0, -1, ""},
  {"Canale 3",      "V",  1.0, 0.0, 2, false, 0, 0, 0, -1, ""},
  {"Canale 4",      "V",  1.0, 0.0, 2, false, 0, 0, 0, -1, ""},
};

// ---------- Colori ----------
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
const uint16_t C_BG      = 0xFFFF;               // sfondo bianco
const uint16_t C_PANEL_L = rgb(214, 218, 216);   // pannello grigio
const uint16_t C_PANEL_R = rgb(160, 228, 220);   // pannello teal chiaro
const uint16_t C_TEAL    = rgb(0, 129, 132);     // teal scuro
const uint16_t C_TEAL_LT = rgb(110, 190, 190);
const uint16_t C_TEXT    = rgb(30, 30, 30);
const uint16_t C_GREY    = rgb(130, 135, 135);
const uint16_t C_FACE    = rgb(246, 246, 240);   // quadrante del voltmetro
const uint16_t C_GREEN   = rgb(60, 170, 120);
const uint16_t C_YELLOW  = rgb(250, 200, 30);
const uint16_t C_ORANGE  = rgb(245, 140, 30);
const uint16_t C_RED     = rgb(220, 40, 40);
const uint16_t C_BLACK   = 0x0000;

GigaDisplay_GFX display;
const int W = 800, H = 480;         // orizzontale

// pannelli
const int LX = 16, LY = 16, LW = 500, LH = 448;
const int RX = 532, RY = 16, RW = 252, RH = 448;

// ---------- Ricezione UART ----------
unsigned long lastReal = 0;          // ultimo dato vero ricevuto dalla UART
bool demoActive = false;
unsigned long rxCount = 0, rxBad = 0;   // righe valide / scartate (debug su USB)
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
  c.vin = v;                          // tensione in ingresso 0..10 V
  c.value = v * c.scale + c.offset;
  c.lastRx = millis();
  lastReal = c.lastRx;
  rxCount++;
}

void pollLink() {
  while (LINK.available()) {
    char c = LINK.read();
    if (c == '\r') continue;
    if (c == '\n') {
      rxBuf[rxLen] = 0;
      if (rxLen) {
        unsigned long before = rxCount;
        parseLine(rxBuf);
        if (rxCount == before) rxBad++;
        if (DEBUG_RX) { Serial.print("RX: "); Serial.println(rxBuf); }
      }
      rxLen = 0;
    } else if (rxLen < sizeof(rxBuf) - 1) {
      rxBuf[rxLen++] = c;
    } else {
      rxLen = 0;                       // riga troppo lunga: scarta
    }
  }
}

void demoTick() {
  demoActive = DEMO && (lastReal == 0 || millis() - lastReal > TIMEOUT_MS);
  if (!demoActive) return;
  float t = millis() / 1000.0f;
  for (int i = 0; i < 4; i++) {
    float vin = 5.0f + 5.0f * sinf(t * 2 * PI / 8.0f + i);   // 0..10 V, periodo 8 s
    ch[i].vin = vin;
    ch[i].value = vin * ch[i].scale + ch[i].offset;
    ch[i].lastRx = millis();
  }
}

float chMin(const Channel &c) { return c.offset; }
float chMax(const Channel &c) { return VIN_FS * c.scale + c.offset; }
bool chAlive(const Channel &c) { return c.lastRx && (millis() - c.lastRx < TIMEOUT_MS); }
float chFrac(const Channel &c) {
  return constrain((c.value - chMin(c)) / (chMax(c) - chMin(c)), 0.0f, 1.0f);
}

// ---------- Testo ----------
int textWidth(const char *s) {
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w;
}
void printAt(const char *s, int x, int y, const GFXfont *f, uint16_t col) {
  display.setFont(f);
  display.setTextColor(col);
  display.setCursor(x, y);
  display.print(s);
}
void printRight(const char *s, int xr, int y, const GFXfont *f, uint16_t col) {
  display.setFont(f);
  printAt(s, xr - textWidth(s), y, f, col);
}
void printCenter(const char *s, int xc, int y, const GFXfont *f, uint16_t col) {
  display.setFont(f);
  printAt(s, xc - textWidth(s) / 2, y, f, col);
}

// ---------- Pannello sinistro ----------
const char *TITLE    = "IZ0JNY TWT Power Supply";
const char *SUBTITLE = "optical insulated display - nanoHam powered";
int STATUS_Y = 132;          // ricalcolati da drawLeftStatic() in base al sottotitolo
// righe dei canali
int ROW_Y0 = 166;
const int ROW_STEP = 68;
const int BAR_X = LX + 24, BAR_W = LW - 48, BAR_H = 18;

uint16_t bandColor(float f) { return f < 0.7f ? C_TEAL : (f < 0.9f ? C_ORANGE : C_RED); }

void drawLeftStatic() {
  display.fillRoundRect(LX, LY, LW, LH, 14, C_PANEL_L);
  printAt(TITLE, LX + 24, LY + 46, &FreeSansBold18pt7b, C_TEAL);
  // sottotitolo su una sola riga, col carattere piu' grande che ci sta nel pannello
  const int SW = LW - 20;
  const GFXfont *sfonts[] = {&FreeSans18pt7b, &FreeSans12pt7b, &FreeSans9pt7b};
  const int sbase[] = {LY + 84, LY + 76, LY + 72};
  int k = 0;
  for (; k < 2; k++) {
    display.setFont(sfonts[k]);
    if (textWidth(SUBTITLE) <= SW) break;
  }
  display.setFont(sfonts[k]);
  printAt(SUBTITLE, LX + (LW - textWidth(SUBTITLE)) / 2, sbase[k], sfonts[k], C_GREY);
  int hrY = sbase[k] + 14;
  display.drawFastHLine(LX + 20, hrY, LW - 40, C_GREY);
  STATUS_Y = hrY + 30;
  ROW_Y0   = STATUS_Y + 30;

  printAt("Status", LX + 24, STATUS_Y, &FreeSansBold12pt7b, C_TEXT);
  printAt("|", LX + 330, STATUS_Y, &FreeSans12pt7b, C_TEXT);
  printRight("115200 bd", LX + LW - 24, STATUS_Y, &FreeSans12pt7b, C_TEXT);

  for (int i = 0; i < 4; i++) {
    int y = ROW_Y0 + i * ROW_STEP;
    printAt(ch[i].name, BAR_X, y + 20, &FreeSans12pt7b, ch[i].enabled ? C_TEXT : C_GREY);
    display.fillRoundRect(BAR_X, y + 32, BAR_W, BAR_H, BAR_H / 2, C_BG);
    if (!ch[i].enabled) printRight("off", LX + LW - 24, y + 20, &FreeSans12pt7b, C_GREY);
    ch[i].lastBar = 0;
    ch[i].lastTxt[0] = 0;
  }
  // credito, in piccolo
  display.setFont(NULL);
  display.setTextSize(1);
  display.setTextColor(C_GREY);
  display.setCursor(LX + LW - 80, LY + LH - 14);
  display.print("dev. IZ0JNY");
}

int lastStatus = -1;
void drawStatus() {
  bool alive = chAlive(ch[METER_CH]);
  int st = demoActive ? 1 : (alive ? 0 : 2);
  if (st == lastStatus) return;
  lastStatus = st;
  const char *txt[] = {"LINK", "DEMO", "NO DATA"};
  uint16_t col[] = {C_GREEN, C_YELLOW, C_RED};
  display.fillRect(LX + 116, STATUS_Y - 22, 200, 30, C_PANEL_L);
  printAt(txt[st], LX + 120, STATUS_Y, &FreeSansBold12pt7b, C_TEXT);
  display.fillCircle(LX + 290, STATUS_Y - 8, 12, col[st]);
}

void drawRow(int i) {
  Channel &c = ch[i];
  if (!c.enabled) return;
  int y = ROW_Y0 + i * ROW_STEP;
  bool alive = chAlive(c);

  char txt[16];
  if (alive) snprintf(txt, sizeof(txt), "%.*f %s", c.decimals, c.value, c.unit);
  else       snprintf(txt, sizeof(txt), "--- %s", c.unit);
  if (strcmp(txt, c.lastTxt)) {
    display.fillRect(LX + LW - 170, y, 150, 28, C_PANEL_L);
    printRight(txt, LX + LW - 24, y + 22, &FreeSansBold12pt7b, alive ? C_TEXT : C_RED);
    strcpy(c.lastTxt, txt);
  }

  // barra: ridisegna solo la parte cambiata
  int w = alive ? (int)(chFrac(c) * BAR_W) : 0;
  int by = y + 32;
  if (w > c.lastBar) display.fillRect(BAR_X + c.lastBar, by, w - c.lastBar, BAR_H, C_TEAL_LT);
  else if (w < c.lastBar) {
    display.fillRect(BAR_X + w, by, c.lastBar - w, BAR_H, C_BG);
    if (c.lastBar >= BAR_W - BAR_H / 2)        // ripristina l'estremo arrotondato
      display.fillRoundRect(BAR_X + BAR_W - BAR_H, by, BAR_H, BAR_H, BAR_H / 2, C_BG);
  }
  c.lastBar = w;
}

// ---------- Indicatore ad arco (pannello destro) ----------
// Arco spesso a 270 gradi: traccia grigia, riempimento colorato proporzionale
// al valore (teal, arancio dal 70%, rosso dal 90%), estremi arrotondati,
// valore grande al centro.
const uint16_t C_TRACK = rgb(205, 235, 230);
const int MX = RX + RW / 2, MY = RY + 130;     // centro
const int AR = 98;                              // raggio della linea media dell'arco
const int AT = 11;                              // meta' spessore
const float SWEEP = 135.0f;                     // +-135 gradi = 270

float gaugeVal = 0;           // valore mostrato (0..1), smorzato
float gaugeDrawn = 0;
uint16_t gaugeCol = 0;
bool gaugeInit = false;

void polar(float frac, float r, int &x, int &y) {
  float a = (-SWEEP + 2 * SWEEP * frac) * PI / 180.0f;
  x = MX + (int)roundf(r * sinf(a));
  y = MY - (int)roundf(r * cosf(a));
}

void arcSeg(float f0, float f1, uint16_t col) {
  for (float f = f0; f <= f1; f += 0.0015f) {
    int x0, y0, x1, y1;
    polar(f, AR - AT, x0, y0);
    polar(f, AR + AT, x1, y1);
    display.drawLine(x0, y0, x1, y1, col);
  }
}

void arcCap(float f, uint16_t col) {
  int x, y;
  polar(f, AR, x, y);
  display.fillCircle(x, y, AT, col);
}

void drawMeterStatic() {
  Channel &c = ch[METER_CH];
  display.fillRoundRect(RX, RY, RW, RH, 14, C_PANEL_R);
  arcSeg(0, 1, C_TRACK);
  arcCap(0, C_TRACK);
  arcCap(1, C_TRACK);
  printCenter(c.unit, MX, MY + 44, &FreeSansBold12pt7b, C_TEXT);
  printCenter(c.name, MX, MY + 96, &FreeSans12pt7b, C_TEXT);
  // simbolo alta tensione: triangolo giallo con fulmine nero
  int tx = MX, tt = RY + 300, tb = RY + 384, hw = 48;
  display.fillTriangle(tx, tt, tx - hw, tb, tx + hw, tb, C_BLACK);
  display.fillTriangle(tx, tt + 10, tx - hw + 9, tb - 5, tx + hw - 9, tb - 5, C_YELLOW);
  int m = (tt + tb) / 2 + 10;
  display.fillTriangle(tx + 8, tt + 22, tx - 12, m + 4, tx + 2, m + 4, C_BLACK);
  display.fillTriangle(tx - 6, tb - 10, tx + 12, m - 4, tx - 1, m - 4, C_BLACK);
  display.fillTriangle(tx - 6, tb - 10, tx - 11, tb - 20, tx - 1, tb - 18, C_BLACK);
  printCenter("HIGH VOLTAGE", MX, RY + 418, &FreeSansBold12pt7b, C_RED);
  gaugeInit = false;
}

char lastMeterTxt[16] = "";
void drawMeter() {
  Channel &c = ch[METER_CH];
  bool alive = chAlive(c);
  float target = alive ? chFrac(c) : 0.0f;
  gaugeVal += (target - gaugeVal) * 0.5f;              // movimento morbido
  float f = gaugeVal;
  uint16_t col = alive ? bandColor(f) : C_GREY;

  if (!gaugeInit || col != gaugeCol) {                  // ridisegno completo
    arcSeg(0, 1, C_TRACK);
    arcCap(1, C_TRACK);
    arcSeg(0, f, col);
    arcCap(0, col);
    arcCap(f, col);
    gaugeInit = true;
  } else if (f > gaugeDrawn + 0.001f) {                 // cresce
    arcSeg(gaugeDrawn, f, col);
    arcCap(f, col);
  } else if (f < gaugeDrawn - 0.001f) {                 // cala
    arcCap(gaugeDrawn, C_TRACK);
    arcSeg(f, gaugeDrawn + 0.03f > 1 ? 1 : gaugeDrawn + 0.03f, C_TRACK);
    if (gaugeDrawn > 0.97f) arcCap(1, C_TRACK);
    arcCap(f, col);
    arcCap(0, col);
  } else {
    f = gaugeDrawn;
  }
  gaugeDrawn = f;
  gaugeCol = col;

  // valore grande al centro
  char txt[16];
  if (alive) snprintf(txt, sizeof(txt), "%.*f", c.decimals, c.value);
  else       snprintf(txt, sizeof(txt), "---");
  if (strcmp(txt, lastMeterTxt)) {
    display.fillRect(MX - 78, MY - 34, 156, 46, C_PANEL_R);
    printCenter(txt, MX, MY + 4, &FreeSansBold24pt7b, alive ? C_TEXT : C_RED);
    strcpy(lastMeterTxt, txt);
  }
}

// ---------- Avvio ----------
void splash() {
  display.fillScreen(C_BG);
  printCenter(TITLE, W / 2, 230, &FreeSansBold24pt7b, C_TEAL);
  printCenter(SUBTITLE, W / 2, 272, &FreeSans12pt7b, C_GREY);
  display.setFont(NULL);
  display.setTextSize(1);
  display.setTextColor(C_GREY);
  display.setCursor(W - 80, H - 14);
  display.print("dev. IZ0JNY");
  delay(2000);
}

void setup() {
  Serial.begin(115200);
  LINK.begin(LINK_BAUD);
  display.begin();
  display.setRotation(1);              // 800x480 orizzontale
  display.setTextWrap(false);          // niente a capo automatico
  splash();
  display.fillScreen(C_BG);
  drawLeftStatic();
  drawMeterStatic();
}

void loop() {
  pollLink();

  static unsigned long lastRefresh = 0;
  if (millis() - lastRefresh >= REFRESH_MS) {
    lastRefresh = millis();
    if (SELFTEST) LINK.print("CH1,5.000\n");
    demoTick();
    drawStatus();
    for (int i = 0; i < 4; i++) drawRow(i);
    drawMeter();
  }

  // riepilogo ogni secondo sulla seriale USB (115200)
  static unsigned long lastLog = 0;
  if (millis() - lastLog >= 1000) {
    lastLog = millis();
    Serial.print(demoActive ? "DEMO" : (chAlive(ch[METER_CH]) ? "LINK" : "NODATA"));
    Serial.print("  righe ok="); Serial.print(rxCount);
    Serial.print(" scartate="); Serial.print(rxBad);
    for (int i = 0; i < 4; i++) {
      Serial.print("  CH"); Serial.print(i + 1); Serial.print("=");
      if (chAlive(ch[i]) && !demoActive) Serial.print(ch[i].vin, 3); else Serial.print("-");
    }
    Serial.println();
  }
}
