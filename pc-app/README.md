# NanoHam PC

Programma per PC che legge le misure delle schede NanoHam (reading board) dalla fibra ottica e le mostra
in una pagina sola, nello stile di EMELab (Streamlit). Per ora è un programma a sé, per le prove;
il nucleo (`nanoham_pc/link.py`) non usa Streamlit e si può spostare in EMELab come scheda.

## Collegamento

NanoHam → TEL0153 → fibra → TEL0153 → cavo USB-TTL 3,3 V (es. Waveshare FT232RNL) → PC.
Windows vede una porta COM. Una porta per ogni NanoHam (10, 24, 47 GHz…).

Protocollo: quello di `control-board/GigaMeasureDisplay/PROTOCOLLO.md`, 115200 8N1, una riga per misura
`CH<n>,<valore>\n` (es. `CH1,7.532`). Un canale senza dati per 1 s diventa `NO DATA`.

## Avvio (Windows)

1. Installa Python 3.10 o più nuovo da python.org (spunta «Add Python to PATH»).
2. Doppio clic su **Avvia NanoHam PC.bat**: la prima volta installa i pacchetti, poi apre il programma.

Oppure da terminale, nella cartella `pc-app`:

```
python -m pip install -r requirements.txt
python -m nanoham_pc.desktop          # finestra propria (pywebview) o browser
streamlit run nanoham_main.py          # solo browser
```

Senza hardware scegli la porta **DEMO**: una NanoHam simulata.

## Pagine

- **🔴 Misure**: valore grande e barra per ogni canale, stato del link (LINK / NO DATA / ERRORE), righe al
  secondo. Bordo rosso e avviso quando un canale esce dalle soglie o resta senza dati.
- **📈 Grafici**: gli ultimi 1–60 minuti. Grafici, titoli, canali e altezza si scelgono in Configurazione.
  Unità diverse nello stesso grafico: la seconda va sull'asse di destra.
- **⚙️ Configurazione**: schede (nome, porta COM, baud, accesa/spenta) e, per ogni canale, nome, tipo,
  unità, rapporto (partitore), offset, decimali, fondo scala, soglie di allarme. Tutto si salva in
  `%APPDATA%\NanoHamPC\settings.json` e si ricarica all'avvio.
- **📒 Log**: un CSV per scheda e per giorno (UTC), separatore `;`, valori già scalati e allarmi attivi.
  Continua a registrare anche se cambi pagina.

Valore mostrato = valore ricevuto × rapporto + offset. Tipi di canale:

- `analog`: tensioni, correnti, temperature (°C)…
- `logic`: uno stato. Ricevuto ≥ 0,5 = ON. Testi a scelta (es. `PTT` / `RX`) e allarme nello stato scelto.

## Canali

| canale | nome | tipo | firmware |
|---|---|---|---|
| CH1 | V Helix (kV) | analog | inviato |
| CH2 | V Master (V) | analog | inviato |
| CH3 | V Slave (V) | analog | inviato |
| CH4 | V Aux (V) | analog | inviato |
| CH5 | T box (°C, LM35 su A6) | analog | inviato |
| CH6 | T heatsink (°C, LM35 su A7) | analog | inviato |
| CH7 | In 1 (GPIO 6) | logic | inviato |
| CH8 | In 2 (GPIO 7) | logic | inviato |

CH5..CH8 li manda `ReadingBoardADC.ino` sulla NanoHam come nel firmware nanoHam_09; la GIGA li ignora.
Con un firmware più vecchio, che manda solo CH1..CH4, togli «Attivo» a CH5..CH8 in Configurazione per non
avere l'allarme NO DATA. Numeri, nomi e tipi si cambiano comunque a piacere.

## Test

```
python -m pytest -q tests
```
