/*
 * ReadingBoardADC.ino
 * nanoHam - reading board
 *
 * Developer: IZ0JNY
 *
 * Legge 4 ingressi 0..10 V da due moduli DFRobot DFR1184 (2 canali ciascuno)
 * collegati in I2C e manda i valori sulla UART verso la GIGA (fibra ottica).
 *
 *   Modulo A  indirizzo 0x48 (switch A1=0 A0=0)  AIN1 -> CH1, AIN2 -> CH2
 *   Modulo B  indirizzo 0x49 (switch A1=0 A0=1)  AIN1 -> CH3, AIN2 -> CH4
 *
 * Protocollo (vedi control-board/GigaMeasureDisplay/PROTOCOLLO.md):
 *   una riga per misura "CH<n>,<volt>\n", 115200 8N1, tensione grezza 0..10 V.
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

// ---------------- configurazione ----------------

// Schede previste:
//  - Arduino UNO R4 WiFi (prove): Serial1 = D0 (RX) / D1 (TX), I2C = pin SDA/SCL
//    vicino ad AREF (Wire). Il connettore Qwiic della R4 e' Wire1, non Wire.
//  - NanoHam (Arduino Nano ESP32): Serial1 su D0/D1, I2C su A4 (SDA) / A5 (SCL).
// Porta seriale verso il modulo in fibra.
#define LINK_SERIAL   Serial1
#define LINK_BAUD     115200
#define I2C_BUS       Wire

// Numero di moduli DFR1184 montati (1 o 2). Con 1 modulo si usano CH1 e CH2.
#define NUM_MODULES   2

// Seriale di debug su USB (commentare per disattivare).
#define DEBUG_SERIAL  Serial

const uint8_t  ADDR_MOD_A      = 0x48;   // CH1, CH2
const uint8_t  ADDR_MOD_B      = 0x49;   // CH3, CH4

const uint8_t  REG_CHANNEL     = 0x20;   // selezione canale (1 o 2)
const uint8_t  REG_DATA        = 0x31;   // 3 byte, valore in 0.01 mV

const uint16_t SETTLE_MS       = 50;     // attesa dopo il cambio canale
const uint16_t RETRY_MS        = 1000;   // nuovo tentativo su un modulo assente

// ---------------- stato ----------------

struct Module {
  uint8_t  addr;
  uint8_t  firstCh;     // numero di canale del protocollo per AIN1
  bool     present;
  bool     skipNext;    // salta la prima lettura dopo una riconnessione
  uint32_t lastTry;
};

Module mods[NUM_MODULES] = {
  { ADDR_MOD_A, 1, false, false, 0 },
#if NUM_MODULES > 1
  { ADDR_MOD_B, 3, false, false, 0 },
#endif
};

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
    DEBUG_SERIAL.print("Modulo 0x");
    DEBUG_SERIAL.print(m.addr, HEX);
    DEBUG_SERIAL.println(" non risponde");
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
    DEBUG_SERIAL.print("Modulo 0x");
    DEBUG_SERIAL.print(m.addr, HEX);
    DEBUG_SERIAL.println(" trovato");
#endif
  }
}

// ---------------- UART ----------------

void sendMeasure(uint8_t ch, float volts) {
  if (volts < 0.0f) volts = 0.0f;
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

// ---------------- setup / loop ----------------

void setup() {
#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.begin(115200);
#endif
  LINK_SERIAL.begin(LINK_BAUD);
  I2C_BUS.begin();
  I2C_BUS.setClock(100000);

  for (Module &m : mods) {
    m.present = probe(m.addr);
    m.lastTry = millis();
#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.print("Modulo 0x");
    DEBUG_SERIAL.print(m.addr, HEX);
    DEBUG_SERIAL.println(m.present ? " OK" : " ASSENTE");
#endif
  }

  switchAll(1);
}

void loop() {
  for (Module &m : mods) tryReconnect(m);

  if (millis() - switchedAt < SETTLE_MS) return;

  // Conversione pronta: leggi l'ingresso corrente da entrambi i moduli.
  for (Module &m : mods) {
    if (!m.present) continue;
    if (m.skipNext) { m.skipNext = false; continue; }
    float v = readVolts(m);
    if (v < 0.0f) { markLost(m); continue; }
    sendMeasure(m.firstCh + (ain - 1), v);
  }

  // Passa all'altro ingresso.
  switchAll(ain == 1 ? 2 : 1);
}
