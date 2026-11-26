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
- Pannello destro: voltmetro analogico del canale `METER_CH` (default CH1, 0..10 kV),
  strumento da pannello classico (quadrante crema, arco di 90°, lancetta nera), lettura digitale e simbolo di alta tensione con la scritta HIGH VOLTAGE.

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
