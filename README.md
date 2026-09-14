# SVEUČILIŠTE JOSIPA JURJA STROSSMAYERA U OSIJEKU
## FAKULTET PRIMIJENJENE MATEMATIKE I INFORMATIKE

**UGRAĐENI SUSTAVI**

## SEMINARSKI RAD
### Pametni sustav upravljanja potrošnjom energije temeljen na ESP32-H2 mikrokontroleru

**Marko Kresić** Osijek, 2026.

---

## Sadržaj
1. [Uvod](#uvod)
2. [Opis sustava](#opis-sustava)
3. [Tehničke specifikacije](#tehničke-specifikacije)
   - [Hardver](#hardver)
   - [Softver](#softver)
4. [Implementacija](#implementacija)
   - [Struktura programa i FreeRTOS](#struktura-programa-i-freertos)
   - [ADC uzorkovanje i izračun efektivne struje](#adc-uzorkovanje-i-izracun-efektivne-struje)
   - [Upravljanje aktuatorom i HW prekidi](#upravljanje-aktuatorom-i-hw-prekidi)
   - [Zigbee bežična komunikacija](#zigbee-bezicna-komunikacija)
5. [Tablica komponenti](#tablica-komponenti)
6. [Zaključak](#zakljucak)
7. [Literatura](#literatura)

---

## 1. Uvod <a id="uvod"></a>
U ovom seminarskom radu opisan je pametni sustav za upravljanje i nadzor potrošnje električne energije u stvarnom vremenu koji koristi mikrokontroler **ESP32-H2** kao središnju upravljačku jedinicu. Cilj projekta bio je izraditi funkcionalan ugrađeni sustav koji mjeri struju priključenog potrošača pomoću analognog senzora **ACS712**, izračunava radnu snagu te na temelju izmjerenih vrijednosti upravlja napajanjem potrošača putem relej modula. Uz lokalno upravljanje fizičkim tipkalom i vanjskim prekidima (ISR), sustav podržava bežičnu komunikaciju putem **Zigbee (IEEE 802.15.4)** protokola. Mjerne vrijednosti i status uređaja šalju se na centralni Zigbee koordinator (npr. SONOFF USB Dongle). Cijeli sustav realiziran je na eksperimentalnoj pločici (*breadboard*) i programiran u **Arduino IDE** okruženju koristeći C++ te **FreeRTOS** operacijski sustav za real-time upravljanje višezadaćnim radom.

## 2. Opis sustava <a id="opis-sustava"></a>
Sustav se sastoji od pet glavnih komponenti koje međusobno komuniciraju kako bi osigurale željenu funkcionalnost:
* **ACS712 (5A)**: Analogni senzor struje koji komunicira s ESP32-H2 putem **ADC** sučelja preko naponskog djelitelja (10 kΩ / 10 kΩ) radi zaštite ulaznog pina.
* **ESP32-H2**: Prima analogne signale, izračunava efektivnu struju i snagu, upravlja FreeRTOS zadaćama, obrađuje vanjske prekide te održava komunikaciju sa Zigbee mrežom.
* **Relej modul (5V s optokouplerom)**: Aktuator spojen na **GPIO4** koji galvanski odvaja upravljački sklop i preklapa strujni krug potrošača[cite: 1].
* **Taktilno tipkalo**: Korisnički ulaz spojen na **GPIO9** koji putem vanjskog prekida (ISR) omogućuje ručno uključivanje i isključivanje releja.
* **LED indikator**: Statusna dioda spojena na **GPIO8** s otpornikom od 220 Ω za vizualnu signalizaciju stanja releja.

## 3. Tehničke specifikacije <a id="tehničke-specifikacije"></a>

### Hardver <a id="hardver"></a>
#### ESP32-H2-DEV-KIT-N4-M
ESP32-H2 se temelji na **32-bitnoj RISC-V** arhitekturi (radni takt 96 MHz)[cite: 1]. Sadrži integrirani IEEE 802.15.4 radijski modul za Zigbee 3.0 i Thread bežičnu komunikaciju[cite: 1].

#### ACS712 senzor struje (5A)
Analogni senzor na bazi Hallovog efekta s osjetljivošću od **185 mV/A**[cite: 1]. Mjeri izmjeničnu ili istosmjernu struju potrošača i daje analogni izlazni napon[cite: 1].

#### Relej modul 5V s optokouplerom
Jednokanalni relejni modul s optičkom izolacijom (*Songle SRD-05VDC-SL-C*)[cite: 1]. Podržava *High-Level* okidanje na 3.3V logičkoj razini[cite: 1].

#### Periferija i ulazno/izlazni krugovi
* **Tipkalo**: Spojeno na GPIO9 uz interni *Pull-Up* otpornik[cite: 1, 2].
* **LEDica**: Spojena na GPIO8 uz serijski zaštitni otpornik od 220 Ω[cite: 1, 2].
* **Naponski djelitelj**: Dva otpornika od 10 kΩ spojena na izlaz ACS712 senzora za spuštanje maksimalnog napona na sigurna 3.3V za ADC ulaz (GPIO1)[cite: 1, 2].

### Softver <a id="softver"></a>
* **Arduino IDE s ESP32 paketom (v3.x)**: Razvoj u C/C++ okruženju, zamjenjujući prvotni ESP-IDF pristup[cite: 1, 2].
* **FreeRTOS**: Korišten za asinkrono izvođenje zadataka, uključujući kreiranje redova (*Queue*) i binarnih semafora (*Semaphore*)[cite: 1, 2].
* **ZBOSS Zigbee 3.0 Stack**: Službena Espressif biblioteka korištena za emulaciju pametnog prekidača (Smart Plug) i komunikaciju sa ZHA integracijom.

## 4. Implementacija <a id="implementacija"></a>

### Struktura programa i FreeRTOS <a id="struktura-programa-i-freertos"></a>
Program je podijeljen na nezavisne FreeRTOS zadaće (*tasks*), vanjski prekid (ISR) i međuzadaćnu komunikaciju putem *Queue-a* i *Semafora*[cite: 1, 2]:
1. `adcMeasureTask`: Uzorkuje ADC ulaz, računa efektivnu struju i snagu te šalje podatak u `xPowerQueue`[cite: 1, 2].
2. `controlTask`: Čeka semafor iz ISR-a tipkala ili nadzire preopterećenje te upravlja relejem i LED-om[cite: 1, 2].
3. **Zigbee Event Callback**: Događaji iz mreže automatski okidaju lambda funkcije (`onLightChange`) za promjenu stanja releja.

### ADC uzorkovanje i izračun efektivne struje (RMS) <a id="adc-uzorkovanje-i-izracun-efektivne-struje"></a>
Za izračun efektivne vrijednosti izmjenične struje primjenjuje se matematički obrazac izračuna korijena srednje kvadratne vrijednosti[cite: 1]:
$$I_{RMS} = \sqrt{\frac{1}{N} \sum_{i=1}^{N} (I_i - I_{off})^2}$$

U kodu se očitava $N=100$ uzoraka unutar jedne periode primjenom standardne `analogRead` funkcije[cite: 1, 2]:
```cpp
long sum_squares = 0;
for (int i = 0; i < ADC_SAMPLES; i++) {
    int raw_val = analogRead(ADC_PIN);
    int diff = raw_val - 2047; // Odstupanje od srednje točke
    sum_squares += (diff * diff);
    vTaskDelay(pdMS_TO_TICKS(2));
}

float mean_square = (float)sum_squares / ADC_SAMPLES;
float rms_raw = sqrt(mean_square);
```

### Upravljanje aktuatorom i HW prekidi

Pritisak na tipkalo okida vanjski HW prekid (GPIO\_INTR\_NEGEDGE) na GPIO9 pinu. Prekidna rutina ne blokira sustav već predaje semafor: C

``` c
void IRAM_ATTR buttonISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xButtonSemaphore, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

```
Također, u control\_task-u je implementirano automatsko isključivanje u slučaju preopterećenja.

### Zigbee bežična komunikacija

Zadaća zigbee\_tx\_task periodički prima podatke iz xPowerQueue te ih šalje prema Zigbee koordinatoru (SONOFF USB Dongle).

Tablica komponenti
------------------

| Komponenta | Model / Opis | Uloga u sustavu |
| :--- | :--- | :--- |
| Mikrokontroler | ESP32-H2-DEV-KIT-N4-M | RISC-V arhitektura, IEEE 802.15.4 (Zigbee/Thread) |
| Senzor struje | ACS712 (5A modul) | Mjerenje struje potrošača i slanje analognog signala |
| Aktuator | 1-Kanalni 5V Relej Modul | Upravljanje uključenjem/isključenjem potrošača[cite: 1] |
| Ulaz | Taktilno tipkalo | Ručna kontrola i GPIO prekid (Interrupt)[cite: 1] |
| Signalizacija | 5mm LED + 220Ω otpornik | Prikaz stanja sustava[cite: 1] |
| Zaštita ADC-a | Djelitelj napona (2x 10kΩ) | Smanjenje izlaznog napona senzora s 5V na sigurna 3.3V za ADC |

Zaključak
---------

U okviru ovog seminarskog rada uspješno je realiziran pametni sustav za nadzor i upravljanje potrošnjom električne energije. Projekt je demonstrirao primjenu **ADC uzorkovanja** za proračun RMS struje, rad s **vanjskim prekidima (ISR)** i **FreeRTOS** mehanizmima unutar **ESP-IDF** okruženja te osnove **Zigbee bežične komunikacije**. Sustav je stabilan, siguran i sve planirane funkcionalnosti su uspješno implementirane.

Literatura
----------

*   Espressif Systems, _ESP32-H2 Technical Reference Manual_, 2024.
    
*   Espressif Systems, _ESP-IDF Programming Guide - ADC & FreeRTOS API_, 2025.
    
*   Allegro MicroSystems, _ACS712 Current Sensor Datasheet_.
