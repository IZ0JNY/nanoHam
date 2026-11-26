/*
 * ReadingBoardADC.ino
 * nanoHam - reading board
 *
 * Developer: IZ0JNY
 *
 * Legge le tensioni e le manda sulla UART verso la GIGA (fibra ottica).
 *
 * NanoHam (Arduino Nano ESP32):
 *   CH1 V Helix   DFR1184 0x48 (switch A1=0 A0=0), ingresso AIN1, 0..10 V
 *   CH2 V Master  ADC NanoHam A1 (V_in2), partitore 1:10
 *   CH3 V Slave   ADC NanoHam A2 (V_in3), partitore 1:5
 *   CH4 V Aux     ADC NanoHam A3 (V_in4), partitore 1:5
 *   Per CH2..CH4 si manda la tensione vera all'ingresso della NanoHam
 *   (pin x partitore), quindi sulla GIGA la scala di questi canali e' 1.
 *
 * Altre schede (UNO R4 WiFi, prove): due DFR1184
 *   Modulo A  0x48 (A1=0 A0=0)  AIN1 -> CH1, AIN2 -> CH2
 *   Modulo B  0x49 (A1=0 A0=1)  AIN1 -> CH3, AIN2 -> CH4
 *
 * Protocollo (vedi control-board/GigaMeasureDisplay/PROTOCOLLO.md):
 *   una riga per misura "CH<n>,<volt>\n", 115200 8N1.
 *
 * Il DFR1184 ha un microcontrollore a bordo davanti all'ADS1115: si scrive il
 * canale nel registro 0x20, il modulo impiega ~50 ms ad aggiornare, poi si
 * leggono 3 byte dal registro 0x31 (valore in centesimi di mV, big endian).
 * La libreria DFRobot aspetta 50 ms con delay() a ogni lettura, quindi 4 canali
 * richiederebbero ~200 ms. Qui i due moduli cambiano canale insieme e si
 * aspetta una sola volta: un giro completo dei 4 canali dura ~100 ms, senza
 * bloccare il loop.
 */

#include <Wire.h>
#include <EEPROM.h>

// Matrice LED 12x8 della UNO R4 WiFi: mostra la tensione di MATRIX_CH.
// Sulle altre schede (NanoHam) questa parte non viene compilata.
#if defined(ARDUINO_UNOR4_WIFI)
#define USE_LED_MATRIX
#include "Arduino_LED_Matrix.h"
#endif

// ---------------- configurazione ----------------

// Schede previste:
//  - Arduino UNO R4 WiFi (prove): Serial1 = D0 (RX) / D1 (TX), I2C = pin SDA/SCL
//    vicino ad AREF (Wire). Il connettore Qwiic della R4 e' Wire1, non Wire.
//  - NanoHam (Arduino Nano ESP32, FQBN arduino:esp32:nano_nora): la UART su
//    D0 (RX) / D1 (TX) e' Serial0; I2C su A4 (SDA) / A5 (SCL), bus condiviso con
//    l'INA219 della NanoHam (0x40, nessun conflitto con 0x48/0x49).
//    Logica a 3,3 V: alimentare DFR1184 e TEL0153 a 3,3 V.
// Porta seriale verso il modulo in fibra.
#if defined(ARDUINO_NANO_ESP32)
#define NANOHAM
#define LINK_SERIAL   Serial0
#else
#define LINK_SERIAL   Serial1
#endif
#define LINK_BAUD     115200
#define I2C_BUS       Wire

// Canale mostrato sulla matrice LED della R4 (testo fisso, senza scorrimento).
const uint8_t  MATRIX_CH       = 1;
const uint16_t MATRIX_STEP_MS  = 200;   // aggiornamento della matrice
// Riepilogo su USB ogni PRINT_MS.
const uint16_t PRINT_MS        = 500;

// Numero di moduli DFR1184 montati (1 o 2).
#ifdef NANOHAM
#define NUM_MODULES   1
#else
#define NUM_MODULES   2
#endif

#ifdef NANOHAM
// Ingressi tensione della NanoHam (come nel firmware nanoHam_09):
// pin, rapporto del partitore, canale del protocollo.
struct NanoInput { uint8_t pin; float ratio; uint8_t ch; float ema; };
NanoInput nanoIn[3] = {
  { A1, 10.0f, 2, -1.0f },   // V_in2 -> V Master
  { A2,  5.0f, 3, -1.0f },   // V_in3 -> V Slave
  { A3,  5.0f, 4, -1.0f },   // V_in4 -> V Aux
};
const float    NANO_EMA_ALPHA  = 0.2f;   // stesso filtro del firmware NanoHam
const uint16_t NANO_SAMPLE_MS  = 10;     // campionamento ingressi NanoHam
const uint16_t NANO_SEND_MS    = 100;    // invio CH2..CH4
#endif

// Seriale di debug su USB (commentare per disattivare).
#define DEBUG_SERIAL  Serial

// La seriale USB non deve mai bloccare il loop: sulla Nano ESP32, se il PC apre
// e chiude la porta senza leggere, il buffer si riempie e print() si blocca,
// fermando anche l'invio in fibra. Si scrive solo se c'e' posto nel buffer.
#ifdef DEBUG_SERIAL
#define DBG_OK()  (DEBUG_SERIAL.availableForWrite() >= 80)
#endif

const uint8_t  ADDR_MOD_A      = 0x48;   // CH1, CH2
const uint8_t  ADDR_MOD_B      = 0x49;   // CH3, CH4

const uint8_t  REG_CHANNEL     = 0x20;   // selezione canale (1 o 2)
const uint8_t  REG_DATA        = 0x31;   // 3 byte, valore in 0.01 mV

const uint16_t SETTLE_MS       = 50;     // attesa dopo il cambio canale
const uint16_t RETRY_MS        = 1000;   // nuovo tentativo su un modulo assente

// ---------------- stato ----------------

struct Module {
  uint8_t  addr;
  uint8_t  ch[2];       // canale del protocollo per AIN1 / AIN2 (0 = non inviato)
  bool     present;
  bool     skipNext;    // salta la prima lettura dopo una riconnessione
  uint32_t lastTry;
};

Module mods[NUM_MODULES] = {
#ifdef NANOHAM
  { ADDR_MOD_A, {1, 0}, false, false, 0 },   // solo AIN1 = V Helix
#else
  { ADDR_MOD_A, {1, 2}, false, false, 0 },
#endif
#if NUM_MODULES > 1
  { ADDR_MOD_B, {3, 4}, false, false, 0 },
#endif
};

float    lastV[5] = { -1, -1, -1, -1, -1 };   // ultima tensione per CH1..CH4 (<0 = nessun dato)
uint32_t lastPrint = 0;

uint8_t  ain = 1;          // ingresso in conversione su entrambi i moduli (1/2)
uint32_t switchedAt = 0;   // millis() del cambio canale

// ---------------- I2C ----------------

bool probe(uint8_t addr) {
  I2C_BUS.beginTransmission(addr);
  return I2C_BUS.endTransmission() == 0;
}

bool selectInput(Module &m, uint8_t input) {
  I2C_BUS.beginTransmission(m.addr);
  I2C_BUS.write(REG_CHANNEL);
  I2C_BUS.write(input);
  return I2C_BUS.endTransmission() == 0;
}

// Ritorna la tensione in volt, o un valore negativo se la lettura fallisce.
float readVolts(Module &m) {
  I2C_BUS.beginTransmission(m.addr);
  I2C_BUS.write(REG_DATA);
  if (I2C_BUS.endTransmission() != 0) return -1.0f;

  if (I2C_BUS.requestFrom(m.addr, (uint8_t)3) != 3) return -1.0f;
  uint32_t raw = ((uint32_t)I2C_BUS.read() << 16);
  raw |= ((uint32_t)I2C_BUS.read() << 8);
  raw |= (uint32_t)I2C_BUS.read();

  return raw / 100000.0f;   // 0.01 mV -> V
}

void markLost(Module &m) {
  if (m.present) {
#ifdef DEBUG_SERIAL
    if (DBG_OK()) {
    DEBUG_SERIAL.print("Modulo 0x");
    DEBUG_SERIAL.print(m.addr, HEX);
    DEBUG_SERIAL.println(" non risponde");
    }
#endif
  }
  m.present = false;
  m.lastTry = millis();
}

void tryReconnect(Module &m) {
  if (m.present || millis() - m.lastTry < RETRY_MS) return;
  m.lastTry = millis();
  if (probe(m.addr) && selectInput(m, ain)) {
    m.present = true;
    m.skipNext = true;  // non ha avuto il tempo di assestarsi
#ifdef DEBUG_SERIAL
    if (DBG_OK()) {
    DEBUG_SERIAL.print("Modulo 0x");
    DEBUG_SERIAL.print(m.addr, HEX);
    DEBUG_SERIAL.println(" trovato");
    }
#endif
  }
}

// ---------------- UART ----------------

// ---------------- calibrazione ----------------
//
// Ogni canale: valore inviato = grezzo * gain + offset
// (grezzo = tensione DFR1184 per CH1, pin x partitore per CH2..CH4).
// Comandi dal monitor seriale USB (115200, fine riga "a capo"):
//   CAL SHOW            mostra gain, offset e valore grezzo attuale
//   CAL <n> LO <volt>   punto basso: tensione nota ora sull'ingresso del canale n
//   CAL <n> HI <volt>   punto alto: calcola gain e offset dai due punti e salva
//   CAL <n> <volt>      un punto solo: corregge solo il gain (offset 0) e salva
//   CAL <n> RESET       gain 1, offset 0 (partitore nominale) e salva
// I valori restano in EEPROM (NVS sulla Nano ESP32) anche a scheda spenta.

struct CalData {
  uint32_t magic;
  float gain[5];
  float offset[5];
};
const uint32_t CAL_MAGIC = 0x4E48434CUL;   // "NHCL"
CalData cal;
float lastRaw[5]  = { -1, -1, -1, -1, -1 };
float loRaw[5]    = { 0, 0, 0, 0, 0 };     // punto basso in attesa del punto alto
float loRef[5]    = { 0, 0, 0, 0, 0 };
bool  loSet[5]    = { false, false, false, false, false };

void calDefaults() {
  cal.magic = CAL_MAGIC;
  for (uint8_t i = 0; i < 5; i++) { cal.gain[i] = 1.0f; cal.offset[i] = 0.0f; }
}

void calLoad() {
#if defined(ARDUINO_ARCH_ESP32)
  EEPROM.begin(sizeof(CalData));
#endif
  EEPROM.get(0, cal);
  bool ok = cal.magic == CAL_MAGIC;
  for (uint8_t i = 1; i <= 4 && ok; i++)
    ok = isfinite(cal.gain[i]) && isfinite(cal.offset[i]) && cal.gain[i] > 0.1f && cal.gain[i] < 10.0f;
  if (!ok) calDefaults();
}

void calSave() {
  EEPROM.put(0, cal);
#if defined(ARDUINO_ARCH_ESP32)
  EEPROM.commit();
#endif
}

void sendMeasure(uint8_t ch, float raw) {
  float volts = raw;
  if (ch >= 1 && ch <= 4) {
    lastRaw[ch] = raw;
    volts = raw * cal.gain[ch] + cal.offset[ch];
  }
  if (volts < 0.0f) volts = 0.0f;
  if (ch <= 4) lastV[ch] = volts;
  LINK_SERIAL.print("CH");
  LINK_SERIAL.print(ch);
  LINK_SERIAL.print(',');
  LINK_SERIAL.print(volts, 3);
  LINK_SERIAL.print('\n');
}

// Cambia ingresso su tutti i moduli presenti e fa partire l'attesa.
void switchAll(uint8_t input) {
  for (Module &m : mods) {
    if (m.present && !selectInput(m, input)) markLost(m);
  }
  ain = input;
  switchedAt = millis();
}

// ---------------- matrice LED (solo R4 WiFi) ----------------

#ifdef USE_LED_MATRIX
ArduinoLEDMatrix matrix;
uint8_t  frame[8][12];
uint32_t mtxLast = 0;

// Cifre 3x5, ogni riga 3 bit (bit 2 = colonna sinistra).
const uint8_t DIGITS[10][5] = {
  {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
  {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
};
const uint8_t TOP = 1;   // prima riga delle cifre (righe 1..5 di 0..7)

void drawDigit(uint8_t d, uint8_t x) {
  for (uint8_t r = 0; r < 5; r++)
    for (uint8_t c = 0; c < 3; c++)
      if (DIGITS[d][r] & (4 >> c)) frame[TOP + r][x + c] = 1;
}

void drawDot(uint8_t x) { frame[TOP + 4][x] = 1; }

// Testo fisso, aggiornato ogni MATRIX_STEP_MS:
//   0..9.99 V  -> "7.53"  (cifra, punto, cifra, cifra)
//   >= 10 V    -> "10.0"
//   nessun dato -> "--"
void matrixTask() {
  if (millis() - mtxLast < MATRIX_STEP_MS) return;
  mtxLast = millis();

  memset(frame, 0, sizeof(frame));
  float v = lastV[MATRIX_CH];
  if (v < 0.0f) {
    for (uint8_t x = 2; x < 5; x++) frame[TOP + 2][x] = 1;
    for (uint8_t x = 7; x < 10; x++) frame[TOP + 2][x] = 1;
  } else if (v < 9.995f) {
    int cv = (int)(v * 100.0f + 0.5f);       // centesimi di volt
    drawDigit(cv / 100, 0);
    drawDot(3);
    drawDigit((cv / 10) % 10, 5);
    drawDigit(cv % 10, 9);
  } else {
    int dv = (int)(v * 10.0f + 0.5f);        // decimi di volt
    if (dv > 999) dv = 999;
    drawDigit(dv / 100, 0);
    drawDigit((dv / 10) % 10, 4);
    drawDot(7);
    drawDigit(dv % 10, 9);
  }
  matrix.renderBitmap(frame, 8, 12);
}
#endif

#ifdef NANOHAM
uint32_t nanoLastSample = 0, nanoLastSend = 0;

// Campiona A1..A3 ogni NANO_SAMPLE_MS (filtro EMA) e manda CH2..CH4
// ogni NANO_SEND_MS. analogReadMilliVolts usa la calibrazione dell'ADC ESP32.
void nanoTask() {
  if (millis() - nanoLastSample >= NANO_SAMPLE_MS) {
    nanoLastSample = millis();
    for (NanoInput &n : nanoIn) {
      float v = analogReadMilliVolts(n.pin) / 1000.0f * n.ratio;
      n.ema = (n.ema < 0.0f) ? v : NANO_EMA_ALPHA * v + (1.0f - NANO_EMA_ALPHA) * n.ema;
    }
  }
  if (millis() - nanoLastSend >= NANO_SEND_MS) {
    nanoLastSend = millis();
    for (NanoInput &n : nanoIn) if (n.ema >= 0.0f) sendMeasure(n.ch, n.ema);
  }
}
#endif

void printSummary() {
#ifdef DEBUG_SERIAL
  if (millis() - lastPrint < PRINT_MS) return;
  lastPrint = millis();
  if (!DBG_OK()) return;   // nessuno legge la porta: salta il riepilogo
  for (uint8_t ch = 1; ch <= 4; ch++) {
    DEBUG_SERIAL.print("CH");
    DEBUG_SERIAL.print(ch);
    DEBUG_SERIAL.print('=');
    if (lastV[ch] < 0.0f) DEBUG_SERIAL.print("--");
    else DEBUG_SERIAL.print(lastV[ch], 3);
    DEBUG_SERIAL.print(ch < 4 ? "  " : "\n");
  }
#endif
}

#ifdef DEBUG_SERIAL
void calPrintCh(uint8_t ch) {
  if (!DBG_OK()) return;
  DEBUG_SERIAL.print("CH"); DEBUG_SERIAL.print(ch);
  DEBUG_SERIAL.print(" gain="); DEBUG_SERIAL.print(cal.gain[ch], 5);
  DEBUG_SERIAL.print(" offset="); DEBUG_SERIAL.print(cal.offset[ch], 4);
  DEBUG_SERIAL.print(" grezzo="); DEBUG_SERIAL.print(lastRaw[ch], 3);
  DEBUG_SERIAL.print(" inviato="); DEBUG_SERIAL.println(lastV[ch], 3);
}

void calReply(const char *msg) {
  if (DBG_OK()) DEBUG_SERIAL.println(msg);
}

void handleCommand(char *line) {
  for (char *c = line; *c; c++) *c = toupper(*c);
  char *tok = strtok(line, " \t");
  if (!tok || strcmp(tok, "CAL") != 0) { calReply("Comandi: CAL SHOW | CAL n LO v | CAL n HI v | CAL n v | CAL n RESET"); return; }

  tok = strtok(NULL, " \t");
  if (!tok || strcmp(tok, "SHOW") == 0) { for (uint8_t i = 1; i <= 4; i++) calPrintCh(i); return; }

  int ch = atoi(tok);
  if (ch < 1 || ch > 4) { calReply("Canale non valido (1..4)"); return; }
  char *arg = strtok(NULL, " \t");
  if (!arg) { calPrintCh(ch); return; }

  if (strcmp(arg, "RESET") == 0) {
    cal.gain[ch] = 1.0f; cal.offset[ch] = 0.0f; loSet[ch] = false;
    calSave(); calReply("Reset e salvato"); calPrintCh(ch); return;
  }

  float raw = lastRaw[ch];
  if (raw < 0.0f) { calReply("Nessuna lettura su questo canale"); return; }

  if (strcmp(arg, "LO") == 0 || strcmp(arg, "HI") == 0) {
    char *val = strtok(NULL, " \t");
    if (!val) { calReply("Manca la tensione"); return; }
    float ref = atof(val);
    if (arg[0] == 'L') {
      loRaw[ch] = raw; loRef[ch] = ref; loSet[ch] = true;
      calReply("Punto basso memorizzato, ora dai la tensione alta e usa CAL n HI v");
      return;
    }
    float rl = loSet[ch] ? loRaw[ch] : 0.0f;
    float vl = loSet[ch] ? loRef[ch] : 0.0f;
    if (fabsf(raw - rl) < 0.05f) { calReply("Punti troppo vicini, allarga la differenza"); return; }
    float g = (ref - vl) / (raw - rl);
    if (g < 0.1f || g > 10.0f) { calReply("Gain fuori range, controlla le tensioni"); return; }
    cal.gain[ch] = g;
    cal.offset[ch] = vl - g * rl;
    loSet[ch] = false;
    calSave(); calReply("Calibrazione a due punti salvata"); calPrintCh(ch); return;
  }

  // un punto: solo gain
  float ref = atof(arg);
  if (raw < 0.05f) { calReply("Tensione troppo bassa per calibrare il gain"); return; }
  float g = ref / raw;
  if (g < 0.1f || g > 10.0f) { calReply("Gain fuori range, controlla la tensione"); return; }
  cal.gain[ch] = g; cal.offset[ch] = 0.0f;
  calSave(); calReply("Calibrazione a un punto salvata"); calPrintCh(ch);
}

char cmdBuf[48];
uint8_t cmdLen = 0;

void commandTask() {
  while (DEBUG_SERIAL.available()) {
    char c = DEBUG_SERIAL.read();
    if (c == '\r') continue;
    if (c == '\n') {
      cmdBuf[cmdLen] = 0;
      if (cmdLen) handleCommand(cmdBuf);
      cmdLen = 0;
    } else if (cmdLen < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;
    }
  }
}
#endif

// ---------------- setup / loop ----------------

void setup() {
#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.begin(115200);
#ifdef NANOHAM
  DEBUG_SERIAL.setTxTimeoutMs(0);   // USB CDC ESP32: non aspettare il PC
#endif
#endif
  LINK_SERIAL.begin(LINK_BAUD);
  calLoad();
#ifdef NANOHAM
  analogReadResolution(12);
  for (NanoInput &n : nanoIn) analogSetPinAttenuation(n.pin, ADC_11db);
#endif
  I2C_BUS.begin();
  I2C_BUS.setClock(100000);

  for (Module &m : mods) {
    m.present = probe(m.addr);
    m.lastTry = millis();
#ifdef DEBUG_SERIAL
    if (DBG_OK()) {
    DEBUG_SERIAL.print("Modulo 0x");
    DEBUG_SERIAL.print(m.addr, HEX);
    DEBUG_SERIAL.println(m.present ? " OK" : " ASSENTE");
    }
#endif
  }

#ifdef USE_LED_MATRIX
  matrix.begin();
#endif

  switchAll(1);
}

void loop() {
#ifdef USE_LED_MATRIX
  matrixTask();
#endif
#ifdef NANOHAM
  nanoTask();
#endif
  printSummary();
#ifdef DEBUG_SERIAL
  commandTask();
#endif
  for (Module &m : mods) tryReconnect(m);

  if (millis() - switchedAt < SETTLE_MS) return;

  // Conversione pronta: leggi l'ingresso corrente da entrambi i moduli.
  for (Module &m : mods) {
    if (!m.present) continue;
    if (m.skipNext) { m.skipNext = false; continue; }
    float v = readVolts(m);
    if (v < 0.0f) { markLost(m); continue; }
    uint8_t ch = m.ch[ain - 1];
    if (ch) sendMeasure(ch, v);
  }

  // Passa all'altro ingresso.
  switchAll(ain == 1 ? 2 : 1);
}
