# Auto-calibrazione con Vref commutata (proposta per la prossima revisione NanoHam)

Stato: **proposta**, da discutere. Niente PCB, solo idea di schema e flusso firmware.

## Obiettivo

Oggi la calibrazione e' `CAL AUTO` guidata: serve un alimentatore e un multimetro, e l'operatore
mette a mano due tensioni su ogni ingresso. Con una tensione di riferimento di precisione a bordo
e qualche commutatore, la scheda puo' calibrarsi da sola (comando `CAL SELF`), anche
periodicamente, senza toccare niente.

Canali da calibrare:

| Canale | ADC | Partitore | Campo ingresso |
|---|---|---|---|
| CH1 V Helix | DFR1184 (ADS1115) AIN1 | interno al modulo | 0-10 V |
| CH2 V Master | ESP32 A1 | 1:10 (oggi) | 0-24 V |
| CH3 V Slave | ESP32 A2 | 1:5 (oggi, max ~16.5 V) | 0-24 V |
| CH4 V Aux | ESP32 A3 | 1:5 (oggi, max ~16.5 V) | 0-24 V |

Nota: per avere davvero 0-24 V su CH2-CH4 conviene portare tutti e tre a circa **1:11**
(24 V -> 2.18 V al pin, ancora nella zona lineare dell'ADC ESP32 con attenuazione 11 dB).

## Idea in una figura

```
                    +12..15 V
                       |
                 +-----------+     R string 0.1 % / 10 ppm
                 | REF 10.000|---+---[R]---+---[R]---+---[R]---+---[R]---+ GND
                 +-----------+   |10 V     |7.5 V    |5 V      |2.5 V    |0 V
                                 |         |         |         |         |
                               +-----------------------------------------------+
   GPIO/I2C (A,B,C,EN) ------> |  MUX 8:1 (CD4051B / ADG1208), VDD = 12..15 V   |
                               +-----------------------------------------------+
                                                  |
                                             +----+----+
                                             | OPA192  |  buffer x1 (bassa impedenza)
                                             +----+----+
                                                  |  CAL BUS
                 +-----------------+--------------+-+---------------+
                 |                 |                |               |
   ingresso  ---[Rs]--+-- K1 --- partitore CH1  K2 --- part. CH2  K3 ...   K4 ...
   campo        TVS   |   (rele' SPDT: NC = campo, NO = CAL BUS)
                      |                 |
                     GND            DFR1184 / ESP32 ADC
```

Ogni ingresso ha un **rele' di segnale SPDT** messo **dopo** le protezioni d'ingresso (resistenza
serie + TVS) e **prima** del partitore. A riposo (bobina spenta) il contatto NC collega il campo:
la scheda misura normalmente anche se il firmware si pianta. Durante la calibrazione il contatto NO
collega il partitore al CAL BUS, che porta una delle tensioni di riferimento scelte dal mux.

## 1. Riferimento di tensione

Consiglio: **10.000 V**, perche' dopo i partitori cade bene in mezzo al campo utile degli ADC.

| Parte | Vout | Precisione iniziale | Deriva | Alimentazione |
|---|---|---|---|---|
| TI REF5010 | 10.000 V | 0.05 % | 3 ppm/C max (grado A) | 10.2-18 V |
| ADI LT1236-10 | 10.000 V | 0.05 % | 5 ppm/C | 11.5-40 V |
| ADI ADR4550 (piano B) | 5.000 V | 0.02 % | 2 ppm/C | 5.5-15 V |
| TI REF5040 (piano C, solo 5 V USB) | 4.096 V | 0.05 % | 3 ppm/C | 4.3-18 V |

- La precisione iniziale conta poco: **si misura una volta il valore vero col multimetro** (meglio
  un 5½ cifre) e lo si salva in NVS (`CAL VREF ...`). Conta la **stabilita'** (deriva termica e nel
  tempo), che con queste parti e' di poche decine di ppm, cioe' molto meglio dell'ADC ESP32.
- Il 10 V richiede un ramo a **12-15 V** sulla scheda (vedi domande in fondo). Se c'e' solo il 5 V
  USB si ripiega sul REF5040, ma con il partitore 1:10/1:11 i punti bassi finiscono sotto i
  ~150 mV al pin, dove l'ADC ESP32 non e' lineare: si perde qualita' sul punto basso.
- Tenere il riferimento sempre acceso (deriva al riavvio e warm-up di qualche minuto) e mettere
  i condensatori indicati nel datasheet su uscita e pin NR/TRIM.

## 2. I punti di calibrazione

Una catena di 4 resistenze uguali sotto il riferimento da' **0 / 2.5 / 5 / 7.5 / 10 V**.

- Resistenze a film sottile 0.1 %, 10-25 ppm/C, stesso valore e stesso lotto (o una rete
  integrata, es. Vishay ACAS / TOMC). Cosi' il **rapporto** e' stabile: anche qui i valori veri
  si misurano una volta col multimetro e si salvano.
- Valore totale 4 x 10 kOhm: 0.25 mA dal riferimento, va bene per tutte le parti in tabella.
- Quali punti usa ogni canale:

| Canale | Punti usati | Tensione al pin ADC |
|---|---|---|
| CH1 (DFR1184, 0-10 V) | 0, 2.5, 5, 7.5 V (il 10 V e' sul bordo del fondo scala, lo eviterei) | — |
| CH2 (1:10) | 2.5, 5, 7.5, 10 V | 0.25-1.0 V |
| CH3/CH4 (1:11 proposto) | 2.5, 5, 7.5, 10 V | 0.23-0.91 V |

- Lo **0 V** serve per l'offset del DFR1184; sugli ingressi ESP32 serve solo come controllo che il
  rele' commuti davvero (la lettura deve crollare), non come punto di calibrazione, perche'
  vicino a 0 l'ADC ESP32 ha una zona morta.
- Con 4 punti si fa una **retta ai minimi quadrati** (gain + offset come oggi), e il residuo dice
  subito se qualcosa non va. Se in futuro serve, con gli stessi punti si puo' passare a una
  correzione a tratti.
- Limite da sapere: sugli ingressi 0-24 V il punto piu' alto e' 10 V, quindi da 10 a 24 V si
  **estrapola**. Sulla Nano ESP32 (ESP32-S3) tra ~1 e ~2.2 V al pin l'ADC e' abbastanza lineare,
  ma una verifica una tantum a 20-24 V con alimentatore + multimetro e' consigliata. Se sulla
  scheda c'e' un ramo >= 24 V, il buffer puo' avere guadagno 2 e dare anche un punto a 20 V
  (vedi domande).

## 3. Selezione del punto: mux + buffer

- Mux 8:1 analogico alimentato a 12-15 V: **CD4051B** (economico, va bene) oppure **ADG1208**
  (Ron ~120 Ohm, leakage pA, migliore). Ingressi: S1=0 V, S2=2.5 V, S3=5 V, S4=7.5 V, S5=10 V,
  S6-S8 liberi (es. a massa o per un punto extra).
- Dopo il mux un **operazionale di precisione in inseguitore** (OPA192, OPA2187: offset
  < 50 uV, ingresso e uscita rail-to-rail, alimentazione fino a 36 V).
- Perche' il buffer: senza, la Ron del mux (100-500 Ohm) fa partitore con l'impedenza d'ingresso
  dei partitori (decine di kOhm) e la sua leakage scorre nelle resistenze della catena. Con il
  buffer il mux vede solo l'ingresso dell'operazionale (pA): l'errore da leakage diventa
  ~100 nA x 2 kOhm = 0.2 mV nel caso peggiore del CD4051 caldo, cioe' < 0.01 % su 2.5 V, e quello
  da Ron sparisce.
- Il buffer pilota tutti e 4 i partitori insieme (pochi mA), quindi si possono calibrare
  **tutti i canali in parallelo**.

## 4. Commutazione degli ingressi: perche' rele' e dove metterli

**Dove:** prima del partitore. Cosi' si calibra tutta la catena (partitore + ADC + riferimento
interno ESP32 o DFR1184). Iniettando al pin ADC si calibra solo l'ADC e resta l'errore delle
resistenze del partitore (0.1-1 %), che e' proprio l'errore piu' grosso dopo quello dell'ADC.

**Con cosa:**

| Opzione | Pro | Contro |
|---|---|---|
| **Rele' di segnale SPDT/DPDT** (Omron G6K-2, Panasonic TQ2, reed Coto 9000) | Nessuna leakage, Ron ~0.1 Ohm (trascurabile), isolamento totale dal campo, sopporta 24 V e transitori, fail-safe a riposo | Ingombro, ~100-150 mW per bobina (o tipi latching), serve driver |
| Switch analogico HV (ADG5436, ADG5419: fino a 40 V, latch-up immune) | Piccolo, veloce, nessuna parte meccanica | VDD deve stare sopra la massima tensione d'ingresso (servirebbero ~30 V), leakage nA e Ron ~10 Ohm in serie al partitore, piu' fragile ai transitori dell'amplificatore |
| Mux tipo CD4051 sugli ingressi | Costa poco | **No**: 24 V oltre i limiti, leakage e Ron in serie al segnale di misura anche durante il funzionamento normale |

Consiglio: **rele' di segnale**. Siamo vicino a un alimentatore HV (V Helix in kV): l'isolamento
meccanico e la Ron trascurabile valgono l'ingombro. Un rele' DPDT puo' anche aprire la massa del
campo, se serve.

Accorgimenti:
- Rele' **dopo** resistenza serie e TVS d'ingresso, cosi' i transitori del campo non arrivano al
  CAL BUS.
- Diodo di ricircolo su ogni bobina (o rele' con diodo integrato).
- Contatto SPDT = break-before-make naturale: il campo non viene mai cortocircuitato sul buffer.
- Piccolo condensatore (es. 100 nF) a valle del partitore gia' presente come filtro: dopo la
  commutazione aspettare ~10 tau prima di leggere.

## 5. Pilotaggio dall'ESP32

Servono 4 linee mux (A, B, C, EN) + 4 rele' = **8 uscite**. I pin liberi della Nano ESP32 sono
pochi (D0/D1 vanno alla fibra, A0-A3/A6/A7 analogici, 4-7 gia' usati), quindi:

- **Espansore I2C a 8 bit** sullo stesso bus del DFR1184 e INA219: TCA9534 o MCP23008 a
  **0x20** (nessun conflitto con 0x40, 0x48, 0x49).
- Rele': **ULN2003A** (o MOSFET 2N7002 singoli) pilotato dall'espansore; bobine a 5 V.
- Pull-down sugli ingressi del ULN2003 e EN del mux disattivato con pull-up: all'accensione
  (espansore ancora in ingresso) **tutti i rele' a riposo = misura normale**.
- Mux a 12-15 V pilotato da logica a 3.3 V: il CD4051B a VDD=15 V vuole VIH ~11 V, quindi
  servono traslatori (o il CD4051 alimentato con VEE/VSS separati); l'**ADG1208** accetta
  logica 3.3 V anche con VDD 15 V: un motivo in piu' per preferirlo.

| Bit TCA9534 | Funzione |
|---|---|
| P0-P2 | MUX A, B, C |
| P3 | MUX EN |
| P4-P7 | Rele' K1-K4 (CH1-CH4) |

## 6. Flusso firmware `CAL SELF`

Una volta sola (a banco, col multimetro):
```
CAL VREF 10.0003 7.5009 5.0002 2.4998     ; valori veri dei punti, salvati in NVS
```

Poi, quando serve (comando, all'avvio dopo il warm-up, o ogni N ore):

1. Controlla che il riferimento sia acceso da almeno 5 min (warm-up).
2. Manda alla GIGA un flag "CAL in corso": il display tiene l'ultimo valore invece di mostrare
   le letture di calibrazione.
3. Mux su 0 V, tutti i rele' K1-K4 su CAL BUS.
4. Per ogni punto (0, 2.5, 5, 7.5, 10 V):
   - seleziona il punto sul mux, attendi ~50 ms (rele' + filtro) e almeno 2 conversioni nuove del
     DFR1184 (~50 ms per cambio canale);
   - media: 64 letture ESP32 per canale, 8-10 letture DFR1184;
   - controllo di stabilita' (stessa logica `stabCheck` di oggi).
5. Rilascia i rele' e torna alla misura normale.
6. Per ogni canale: retta ai minimi quadrati sui punti validi -> gain, offset, residuo.
7. Verifiche prima di salvare:
   - gain entro +-5 % del nominale, offset entro limiti ragionevoli;
   - residuo massimo < es. 0.2 % del fondo scala (se no: ADC non lineare o punto rovinato);
   - punto 0 V letto davvero basso (se no: rele' bloccato sul campo -> errore);
   - variazione rispetto alla calibrazione precedente < es. 0.5 % (se di piu': salva lo stesso ma
     segnala, potrebbe essere un guasto o un riferimento andato).
8. Salva in NVS (stessa struttura `CalData`, piu' contatore e "eta'" dell'ultima calibrazione) e
   stampa il riepilogo, come `CAL SHOW`.

Tempo totale stimato: < 2 s per tutti e 4 i canali insieme.

Comandi nuovi proposti: `CAL SELF`, `CAL VREF ...`, `CAL SELF EVERY <ore>` (0 = disattivo).
`CAL AUTO` guidato resta come fallback e per la verifica a 24 V.

## 7. Bonus: canale di verifica

Se avanza un ingresso del mux o un canale ADC, collegare il CAL BUS anche a un ingresso libero
del DFR1184 (AIN2 del modulo A, oggi CH2 non usa il DFR): il DFR1184 (16 bit) misura il CAL BUS a
ogni calibrazione e fa da secondo testimone. Se riferimento e DFR1184 cominciano a non andare
d'accordo, uno dei due sta derivando.

## Domande aperte (servono per chiudere lo schema)

1. Che **alimentazioni** ci sono sulla NanoHam rev. successiva? Serve un 12-15 V per riferimento
   10 V, mux e buffer. C'e' anche un ramo >= 24 V (per un punto a 20 V)?
2. **Rele'** va bene come ingombro/costo, o preferisci tutto a stato solido?
3. I partitori di CH2-CH4 passano tutti a **1:11** (0-24 V)?
4. Calibrazione con l'amplificatore **acceso** (misura sospesa < 2 s) va bene, o solo a
   amplificatore spento / all'avvio?
