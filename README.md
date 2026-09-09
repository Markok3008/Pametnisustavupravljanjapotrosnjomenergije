# FAKULTET PRIMIJENJENE MATEMATIKE I INFORMATIKE OSIJEK
## UGRAĐENI SUSTAVI

### SEMINARSKI RAD / PROJEKT
# Pametni sustav upravljanja potrošnjom energije temeljen na ESP32-H2

**Nastavnik:** dr. sc. Juraj Benić  
**Asistent:** Mislav Milinković  
**Student:** [Vaše Ime i Prezime]  
Osijek, 2026.

---

## Sadržaj
1. [Uvod](#uvod)
2. [Opis sustava](#opis-sustava)
3. [Hardverska arhitektura i komponente](#hardverska-arhitektura-i-komponente)
4. [Softverska arhitektura i FreeRTOS](#softverska-arhitektura-i-freertos)
5. [Shema spajanja i pinout](#shema-spajanja-i-pinout)
6. [Upute za prevođenje i pokretanje (ESP-IDF)](#upute-za-prevođenje-i-pokretanje-esp-idf)
7. [Bodovna tablica projekta](#bodovna-tablica-projekta)
8. [Zaključak](#zaključak)
9. [Literatura](#literatura)

---

## Uvod
U sklopu ovog projekta izrađen je ugrađeni sustav za nadzor i upravljanje potrošnjom električne energije u stvarnom vremenu. Sustav se temelji na mikrokontroleru **ESP32-H2** (RISC-V arhitektura) te koristi **ACS712** senzor struje za izračun efektivne struje ($I_{RMS}$) i snage. Korisnik može upravljati priključenim potrošačem putem relej modula s optokouplerom ili fizičkog tipkala, dok se mjerne vrijednosti bežično šalju na Zigbee koordinator.

Projekt je u potpunosti razvijen u službenom **ESP-IDF** razvojnom okruženju u jeziku C, uz korištenje **FreeRTOS** operacijskog sustava za real-time upravljanje zadaćama i optimizaciju potrošnje energije.

---

## Opis sustava
Sustav obavlja sljedeće ključne funkcionalnosti:
* **Mjerenje u stvarnom vremenu**: Analogni ulaz (ADC) mikrokontrolera kontinuirano očitava naponski signal sa senzora ACS712 te izračunava radnu snagu ($P = U \cdot I$).
* **Aktualizacija i zaštita**: Relej modul omogucuje ručno ili automatsko isključivanje potrošača u slučaju preopterećenja.
* **Bežična mesh komunikacija**: Integrirani IEEE 802.15.4 radijski sklop šalje izmjerenu snagu na Zigbee koordinator (npr. SONOFF USB Dongle ili drugi ESP32-H2 čvor).
* **Signalizacija**: LED indikator prikazuje trenutni status rada sustava i mrežnu povezanost.
* **Upravljanje energijom**: Implementirani su FreeRTOS neblokirajući tajmeri i opcije ulaska u *deep-sleep* način rada radi štednje energije.

---

## Hardverska arhitektura i komponente

| Komponenta | Model / Opis | Uloga u sustavu |
| :--- | :--- | :--- |
| Mikrokontroler | ESP32-H2-DEV-KIT-N4-M | RISC-V arhitektura, IEEE 802.15.4 (Zigbee/Thread) |
| Senzor struje | ACS712 (5A modul) | Mjerenje struje potrošača i slanje analognog signala |
| Aktuator | 1-Kanalni 5V Relej Modul | Upravljanje uključenjem/isključenjem potrošača |
| Ulaz | Taktilno tipkalo | Ručna kontrola i GPIO prekid (Interrupt) |
| Signalizacija | 5mm LED + 220Ω otpornik | Prikaz stanja sustava |
| Zaštita ADC-a | Djelitelj napona (2x 10kΩ) | Smanjenje izlaznog napona senzora s 5V na sigurna 3.3V za ADC |

---

## Softverska arhitektura i FreeRTOS

Aplikacija je strukturirana u nekoliko neovisnih FreeRTOS zadaća koje komuniciraju putem redova poruka (*Queues*) i semafora (*Semaphores*):

1. **`adc_measure_task`**: Periodički (svakih 500 ms) uzorkuje ADC ulaz, primjenjuje kalibraciju te šalje izračunatu snagu u `xPowerQueue`.
2. **`control_task`**: Čeka poruke s tipkala ili mrežne naredbe te upravlja GPIO pinom releja i LED indikatorom.
3. **`zigbee_tx_task`**: Preuzima podatke o snazi iz reda poruka i šalje ih putem ESP-Zigbee stoga.
4. **`gpio_isr_handler`**: Vanjski prekid na pritisak tipkala koji putem semafora `xButtonSemaphore` trenutačno mijenja stanje releja.

---

## Shema spajanja i pinout
