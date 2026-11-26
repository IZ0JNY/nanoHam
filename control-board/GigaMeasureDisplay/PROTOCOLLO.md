# Protocollo UART reading board → GIGA

- Linea: UART 115200 baud, 8N1, attraverso i moduli in fibra ottica.
- Lato GIGA: `Serial1`, pin D0 (RX1) e D1 (TX1).
- Una riga ASCII per misura, terminata da `\n` (il `\r` viene ignorato):

```
CH<n>,<valore>\n
```

- `n` = 1..4, `valore` già in unità ingegneristiche (es. volt), con punto decimale.
- Esempio: `CH1,7.532`
- La reading board manda ogni canale ogni 100 ms (o più spesso). Il display si aggiorna ogni 100 ms.
- Se un canale non riceve dati per 1 s il display mostra `-----` e `NODATA`.

Nome della misura, unità e fondo scala della barra si impostano nella tabella `ch[]`
in `GigaMeasureDisplay.ino`.

## Prova senza la reading board

Collega un adattatore USB-seriale 3,3 V a D0 (RX1) e GND, apri un terminale a 115200
e scrivi `CH1,5.000` + Invio.
