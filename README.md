SVEUČILIŠTE JOSIPA JURJA STROSSMAYERA U OSIJEKU
===============================================

FAKULTET PRIMIJENJENE MATEMATIKE I INFORMATIKE
==============================================

**UGRAĐENI SUSTAVI**

SEMINARSKI RAD
--------------

### Pametni sustav upravljanja potrošnjom energije temeljen na ESP32-H2 mikrokontroleru

**Marko Kresić** Osijek, 2026.

Sadržaj
-------

1.  [Uvod](https://www.google.com/search?q=#uvod)
    
2.  [Opis sustava](https://www.google.com/search?q=#opis-sustava)
    
3.  [Tehničke specifikacije](https://www.google.com/search?q=#tehničke-specifikacije)
    
    *   [Hardver](https://www.google.com/search?q=#hardver)
        
    *   [Softver](https://www.google.com/search?q=#softver)
        
4.  [Implementacija](https://www.google.com/search?q=#implementacija)
    
    *   [Struktura programa i FreeRTOS](https://www.google.com/search?q=#struktura-programa-i-freertos)
        
    *   [ADC uzorkovanje i izračun efektivne struje](https://www.google.com/search?q=#adc-uzorkovanje-i-izracun-efektivne-struje)
        
    *   [Upravljanje aktuatorom i HW prekidi](https://www.google.com/search?q=#upravljanje-aktuatorom-i-hw-prekidi)
        
    *   [Zigbee bežična komunikacija](https://www.google.com/search?q=#zigbee-bezicna-komunikacija)
        
5.  [Tablica komponenti](https://www.google.com/search?q=#tablica-komponenti)
    
6.  [Zaključak](https://www.google.com/search?q=#zakljucak)
    
7.  [Literatura](https://www.google.com/search?q=#literatura)
    

Uvod
----

U ovom seminarskom radu opisan je pametni sustav za upravljanje i nadzor potrošnje električne energije u stvarnom vremenu koji koristi mikrokontroler **ESP32-H2** kao središnju upravljačku jedinicu. Cilj projekta bio je izraditi funkcionalan ugrađeni sustav koji mjeri struju priključenog potrošača pomoću analognog senzora **ACS712**, izračunava radnu snagu te na temelju izmjerenih vrijednosti upravlja napajanjem potrošača putem relej modula. Uz lokalno upravljanje fizičkim tipkalom i vanjskim prekidima (ISR), sustav podržava bežičnu komunikaciju putem **Zigbee (IEEE 802.15.4)** protokola. Mjerne vrijednosti i status uređaja šalju se na centralni Zigbee koordinator (npr. SONOFF USB Dongle). Cijeli sustav realiziran je na eksperimentalnoj pločici (_breadboard_) i programiran u izvornom **ESP-IDF** okruženju koristeći **FreeRTOS** operacijski sustav za real-time upravljanje višezadaćnim radom\[cite: 1, 2\].

Opis sustava
------------

Sustav se sastoji od pet glavnih komponenti koje međusobno komuniciraju kako bi osigurale željenu funkcionalnost:

*   **ACS712 (5A)**: Analogni senzor struje koji komunicira s ESP32-H2 putem **ADC1** sučelja preko naponskog djelitelja (10 kΩ / 10 kΩ) radi zaštite ulaznog pina.
    
*   **ESP32-H2**: Prima analogne signale, izračunava efektivnu struju i snagu, upravlja FreeRTOS zadaćama, obrađuje vanjske prekide te šalje podatke na Zigbee mrežu.
    
*   **Relej modul (5V s optokouplerom)**: Aktuator spojen na **GPIO4** koji galvanski odvaja upravljački sklop i preklapa strujni krug potrošača.
    
*   **Taktilno tipkalo**: Korisnički ulaz spojen na **GPIO9** koji putem vanjskog prekida (ISR) omogućuje ručno uključivanje i isključivanje releja.
    
*   **LED indikator**: Statusna dioda spojena na **GPIO8** s otpornikom od 220 Ω za vizualnu signalizaciju stanja releja.
    

Tehničke specifikacije
----------------------

### Hardver

#### ESP32-H2-DEV-KIT-N4-M

ESP32-H2 se temelji na **32-bitnoj RISC-V** arhitekturi (radni takt 96 MHz). Sadrži integrirani IEEE 802.15.4 radijski modul za Zigbee 3.0 i Thread bežičnu komunikaciju.

#### ACS712 senzor struje (5A)

Analogni senzor na bazi Hallovog efekta s osjetljivošću od **185 mV/A**. Mjeri izmjeničnu ili istosmjernu struju potrošača i daje analogni izlazni napon.

#### Relej modul 5V s optokouplerom

Jednokanalni relejni modul s optičkom izolacijom (_Songle SRD-05VDC-SL-C_). Podržava _High-Level_ okidanje na 3.3V logičkoj razini.

#### Periferija i ulazno/izlazni krugovi

*   **Tipkalo**: Spojeno na GPIO9 uz interni _Pull-Up_ otpornik.
    
*   **LEDica**: Spojena na GPIO8 uz serijski zaštitni otpornik od 220 Ω.
    
*   **Naponski djelitelj**: Dva otpornika od 10 kΩ spojena na izlaz ACS712 senzora za spuštanje maksimalnog napona na sigurna 3.3V za ADC ulaz (GPIO1).
    

### Softver

*   **ESP-IDF v5.x i FreeRTOS**: Razvoj u C jeziku u VS Code / ESP-IDF okruženju. Korišteni upravljački moduli: esp\_adc, driver/gpio, freertos/task.h, freertos/queue.h, freertos/semphr.h.
    
*   **Zigbee 3.0 Stack**: Implementacija profila za bežično slanje telemetrijskih podataka na koordinator.
    

Implementacija
--------------

### Struktura programa i FreeRTOS

Program je podijeljen na tri nezavisne FreeRTOS zadaće (_tasks_), vanjski prekid (ISR) i međuzadaćnu komunikaciju putem _Queue-a_ i _Semafora_:

1.  adc\_measure\_task: Svakih 500 ms uzorkuje ADC1 ulaz, računa efektivnu struju i snagu te šalje podatak u xPowerQueue.
    
2.  control\_task: Čeka semafor iz ISR-a tipkala ili nadzire preopterećenje te upravlja relejem i LED-om.
    
3.  zigbee\_tx\_task: Preuzima izmjerenu snagu i šalje je na bežičnu Zigbee mrežu.
    

### ADC uzorkovanje i izračun efektivne struje (RMS)
Za izračun efektivne vrijednosti izmjenične struje primjenjuje se matematički obrazac izračuna korijena srednje kvadratne vrijednosti:
$$I_{RMS} = \sqrt{\frac{1}{N} \sum_{i=1}^{N} (I_i - I_{off})^2}$$

U kôdu se očitava $N=100$ uzoraka unutar jedne periode:
'''c
long sum_squares = 0;
for (int i = 0; i < ADC_SAMPLES; i++) {
    adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw_val);
    int diff = raw_val - 2047; // Odstupanje od srednje točke (1.65V)
    sum_squares += (diff * diff);
    vTaskDelay(pdMS_TO_TICKS(2));
}
float mean_square = (float)sum_squares / ADC_SAMPLES;
float rms_raw = sqrtf(mean_square);


### Upravljanje aktuatorom i HW prekidi

Pritisak na tipkalo okida vanjski HW prekid (GPIO\_INTR\_NEGEDGE) na GPIO9 pinu. Prekidna rutina ne blokira sustav već predaje semafor: C

static void IRAM_ATTR gpio_button_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xButtonSemaphore, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

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
