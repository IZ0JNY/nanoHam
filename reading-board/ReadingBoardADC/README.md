# Reading board: ADC DFR1184

Developer: IZ0JNY

Due moduli DFRobot DFR1184 (Gravity 0-10V 15-bit, 2 canali, ADS1115 + MCU a bordo)
sullo stesso bus I2C. Totale 4 ingressi 0..10 V → CH1..CH4 del protocollo verso la GIGA.

## Indirizzi I2C

Su ogni modulo:

1. Selettore **I2C/UART** su **I2C**.
2. Selettore indirizzo (A1/A0, la tabella è stampata sul retro della scheda):

| A1 | A0 | indirizzo |
|----|----|-----------|
| 0  | 0  | 0x48 (default) |
| 0  | 1  | 0x49 |
| 1  | 0  | 0x4A |
| 1  | 1  | 0x4B |

Configurazione scelta:

| modulo | A1 | A0 | indirizzo | AIN1 | AIN2 |
|--------|----|----|-----------|------|------|
| A      | 0  | 0  | 0x48      | CH1  | CH2  |
| B      | 0  | 1  | 0x49      | CH3  | CH4  |

Sposta i selettori **a scheda spenta**: il modulo legge selettore I2C/UART e indirizzo solo
all'accensione. Il reset del microcontrollore non basta, bisogna togliere l'alimentazione
(provato sulla UNO R4: dopo il cambio su I2C il modulo ha risposto solo staccando l'USB).
L'indirizzo non si cambia da software.

## Collegamento

| modulo | MCU |
|--------|-----|
| D/T    | SDA |
| C/R    | SCL |
| VCC    | 3,3 V o 5 V, la stessa tensione logica del MCU |
| GND    | GND |

I due moduli vanno in parallelo sul bus (SDA con SDA, SCL con SCL).
Ingressi: AIN1 / AIN2 positivo, GND ingresso negativo (in comune con la massa).

## Tempi

Il modulo vuole ~50 ms dopo ogni cambio canale (la libreria DFRobot usa `delay(50)`
a ogni lettura, quindi 4 canali = ~200 ms). Il firmware non usa la libreria: cambia
canale sui due moduli insieme e aspetta una volta sola, così ogni canale si aggiorna
ogni ~100 ms senza bloccare il loop. `SETTLE_MS` nello sketch regola l'attesa.

## Uscita

UART `LINK_SERIAL` su D0/D1 (`Serial1` sulla UNO R4, `Serial0` sulla Nano ESP32 della NanoHam), 115200 8N1, righe `CH<n>,<volt>\n`
come da `control-board/GigaMeasureDisplay/PROTOCOLLO.md`.
Se un modulo non risponde i suoi canali smettono di essere inviati (la GIGA mostra
`NODATA` dopo 1 s) e il firmware riprova a trovarlo ogni secondo.

## Modulo in fibra DFRobot TEL0153

Uno per scheda. Alimentazione e segnali 3,3..5 V, max 2 000 000 baud (115200 va benissimo),
fibra monomodale 1310 nm con connettori SC, **doppia** (una fibra per verso).

- Alimenta ogni modulo alla tensione logica della sua scheda: sul lato GIGA a **3,3 V**
  (i pin della GIGA non sono 5 V tolerant), sulla reading board alla tensione del suo MCU.
- TX e RX incrociati: TX modulo → RX MCU, RX modulo → TX MCU. Anche le due fibre vanno
  incrociate tra i due moduli (TX di uno sull'RX dell'altro).
- Lato GIGA: TX modulo → D0 (RX1), RX modulo → D1 (TX1), GND in comune.
- LED SD acceso = fibra scollegata.

## NanoHam (Arduino Nano ESP32)

- FQBN `arduino:esp32:nano_nora`.
- I2C su A4 (SDA) / A5 (SCL), lo stesso bus dell'INA219 di bordo (0x40).
- Fibra su D0 (RX) / D1 (TX), `Serial0`.
- La Nano ESP32 lavora a 3,3 V e i pin non reggono i 5 V: DFR1184 e TEL0153 a **3,3 V**.

## Calibrazione

Dal monitor seriale dell'IDE Arduino (115200, fine riga "a capo" / newline), con la scheda
collegata in USB. Valore inviato = grezzo × gain + offset, per ogni canale. I valori si
salvano in memoria e restano anche a scheda spenta.

| comando | cosa fa |
|---|---|
| `CAL SHOW` | mostra gain, offset, valore grezzo e inviato di CH1..CH4 |
| `CAL 2 LO 1.000` | punto basso: sull'ingresso di CH2 ci sono 1,000 V (misurati col tester) |
| `CAL 2 HI 12.000` | punto alto: calcola gain e offset dai due punti e salva |
| `CAL 2 12.000` | un punto solo: corregge solo il gain (offset 0) e salva |
| `CAL 2 RESET` | torna al partitore nominale (gain 1, offset 0) e salva |

Procedura a due punti: dai la tensione bassa, aspetta un secondo che si stabilizzi, `CAL n LO v`;
poi la tensione alta, `CAL n HI v`. Se salti il punto basso, `HI` usa 0 V = 0 come punto basso.
Per CH1 (V Helix) la tensione è quella sull'ingresso AIN1 del DFR1184 (0..10 V).
