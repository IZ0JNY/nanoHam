# Protocollo UART reading board → GIGA

- Linea: UART 115200 baud, 8N1, attraverso i moduli in fibra ottica.
- Lato GIGA: `Serial1`, pin D0 (RX0) e D1 (TX0), serigrafati RX0/TX0 (non RX1/TX1, che sono Serial2).
- Una riga ASCII per misura, terminata da `\n` (il `\r` viene ignorato):

```
CH<n>,<valore>\n
```

- `n` = 1..4, `valore` = tensione letta all'ingresso ADC, 0..10 V, con punto decimale.
- Esempio: `CH1,7.532`
- La reading board manda ogni canale ogni 100 ms (o più spesso). Il display si aggiorna ogni 100 ms.
- Se un canale non riceve dati per 1 s il display mostra `-----` e `NODATA`.
- Con `DEMO 1` (default), finché non arrivano dati veri la GIGA genera un'onda 0..10 V di prova e mostra `DEMO`.

## Configurazione dei canali (lato GIGA)

Nella tabella `ch[]` di `GigaMeasureDisplay.ino`, per ogni canale:

| campo | significato |
|---|---|
| `name` | nome della misura |
| `unit` | unità mostrata |
| `scale` | rapporto di scala: valore mostrato = Vin × scale + offset |
| `offset` | offset in unità mostrate |
| `fullScale` | fondo scala della barra e dell'arco, in unità mostrate |
| `decimals` | decimali mostrati |
| `enabled` | canale attivo |

Esempio: partitore 10:1 davanti a una tensione 0..100 V → `scale = 10.0`, `unit = "V"`.
Il fondo scala della barra si calcola da solo (0..10 V × scale + offset).

## Prova senza la reading board

Collega un adattatore USB-seriale 3,3 V a D0 (RX0) e GND, apri un terminale a 115200
e scrivi `CH1,5.000` + Invio.

## Grafica

- Pannello sinistro: titolo "IZ0JNY TWT Power Supply", sottotitolo "optical insulated display",
  stato del link (LINK / DEMO / NO DATA) e una riga con barra a tutta larghezza per ognuno dei 4 canali.
- Pannello destro: indicatore ad arco a 270° del canale `METER_CH` (default CH1, 0..10 kV),
  traccia grigia e riempimento colorato proporzionale al valore, valore grande al centro con 3 decimali e simbolo di alta tensione con la scritta HIGH VOLTAGE.

## Test in loopback

Con `SELFTEST 1` la GIGA trasmette `CH1,5.000` su Serial1 (D1) ogni 100 ms.
Collegando il TX ottico del modulo della GIGA al suo stesso RX ottico (una sola fibra),
oppure D1 a D0 con un filo, il display deve passare a LINK e mostrare 5 kV.
Se succede, il lato GIGA (pin, modulo, software) è a posto.

## Cablaggio dei moduli in fibra (verificato il 2026-11-26)

| scheda | pin TX del modulo | pin RX del modulo |
|---|---|---|
| GIGA R1 | D0 (RX0) | D1 (TX0) |
| UNO R4 WiFi | D0 (RX) | D1 (TX) |

- Fibre incrociate: TX ottico della R4 → RX ottico della GIGA, TX ottico della GIGA → RX ottico della R4.
- VCC e GND di ogni modulo dalla propria scheda. Sul lato GIGA l'uscita del modulo verso D0 non deve superare 3,3 V.
- Dopo aver ricablato, spegni e riaccendi le schede.
- I LED dei moduli fissi indicano solo che la luce arriva (UART a riposo); lampeggiano quando passano dati.
- Se il display torna in DEMO e i LED dei moduli non lampeggiano, controlla prima i fili dupont su R4 pin 1 e GIGA pin 0: un contatto lento basta a interrompere il link. Per l'uso definitivo meglio connettori saldati o a vite.

## Canali e touch

| canale | nome | sorgente | unità | fondo scala |
|---|---|---|---|---|
| CH1 | V Helix | DFR1184, 0..10 V = 0..10 kV | kV | 10 |
| CH2 | V Master | nanoHam A1 (partitore 1:10), volt reali | V | 24 |
| CH3 | V Slave | nanoHam A2 (partitore 1:5), volt reali | V | 24 |
| CH4 | V Aux | nanoHam A3 (partitore 1:5), volt reali | V | 24 |

Toccando una riga a sinistra, l'arco a destra mostra quel canale (marcatore teal accanto alla riga).
Il simbolo HIGH VOLTAGE compare solo con V Helix.

## Canali in più e programma per PC

La NanoHam manda anche queste misure, con lo stesso formato; la GIGA le ignora (scarta `n > 4`):

| canale | nome | valore |
|---|---|---|
| CH5 | T box | °C (LM35 su A6) |
| CH6 | T heatsink | °C (LM35 su A7) |
| CH7 | In 1 | 0 o 1 (GPIO 6) |
| CH8 | In 2 | 0 o 1 (GPIO 7) |

Come nel firmware nanoHam_09: temperatura = volt al pin × 100 (LM35, 10 mV/°C), ingressi senza pull-up,
HIGH = 1. Valori ogni 100 ms, come CH2..CH4. Il programma per PC in `pc-app/` legge qualsiasi `CH<n>` con
nome, unità, scala e soglie modificabili.
