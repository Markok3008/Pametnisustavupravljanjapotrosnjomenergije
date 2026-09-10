/*
 * Projekt: Pametni sustav upravljanja potrošnjom energije temeljen na ESP32-H2
 * Kolegij: Ugrađeni sustavi 
 * 
 * Opis:
 *  - ACS712 očitavanje struje preko ADC1 drajvera
 *  - Izračun efektivne struje (IRMS) i radne snage (P = U * I)
 *  - Upravljanje relejem i LED indikatorom putem GPIO-a
 *  - Rukovanje tipkalom preko vanjskog prekida (ISR) i FreeRTOS semafora
 *  - Međuzadaćna komunikacija preko FreeRTOS Queue-a
 */

#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "nvs_flash.h"

// ── Hardverske konfiguracije i GPIO pinovi ─────────────────────────────
#define GPIO_RELAY          GPIO_NUM_4
#define GPIO_LED            GPIO_NUM_8
#define GPIO_BUTTON         GPIO_NUM_9
#define ADC_CHANNEL         ADC_CHANNEL_0   // GPIO 1 na ESP32-H2

#define MAINS_VOLTAGE       230.0f          // Mrežni napon (V)
#define ACS712_SENSITIVITY  0.185f          // Osjetljivost za ACS712-05B (185 mV/A)
#define ADC_SAMPLES         100             // Broj uzoraka za kalkulaciju RMS-a

static const char *TAG = "SMART_POWER_CTRL";

// ── Globale i FreeRTOS ručke ───────────────────────────────────────────
static QueueHandle_t     xPowerQueue = NULL;
static SemaphoreHandle_t xButtonSemaphore = NULL;
static bool              g_relay_state = false;

// ── ISR Handler za tipkalo ─────────────────────────────────────────────
static void IRAM_ATTR gpio_button_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xButtonSemaphore, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ── Task 1: ADC Mjerenje struje i procjena snage ───────────────────────
static void adc_measure_task(void *pvParameters) {
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config));

    ESP_LOGI(TAG, "ADC1 uspješno inicijaliziran.");

    while (1) {
        long sum_squares = 0;
        int raw_val = 0;

        // Uzorkovanje za RMS izračun struje
        for (int i = 0; i < ADC_SAMPLES; i++) {
            adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw_val);
            // Središnja točka za 3.3V ADC pri 12-bitnoj rezoluciji (4095 / 2 = 2047)
            int diff = raw_val - 2047; 
            sum_squares += (diff * diff);
            vTaskDelay(pdMS_TO_TICKS(2));
        }

        float mean_square = (float)sum_squares / ADC_SAMPLES;
        float rms_raw = sqrtf(mean_square);
        
        // Pretvorba sirovog ADC odstupanja u napon (mV) i struju (A)
        float voltage_rms = (rms_raw / 4095.0f) * 3300.0f; 
        float current_rms = voltage_rms / (ACS712_SENSITIVITY * 1000.0f);
        
        // Djelitelj napona (10k/10k) dijeli ulaz s 2, pa množimo natrag s 2
        current_rms *= 2.0f; 

        if (current_rms < 0.05f) current_rms = 0.0f; // Filtriranje šuma

        float power = MAINS_VOLTAGE * current_rms;   // P = U * I (W)

        ESP_LOGI(TAG, "[SENZOR] Struja: %.2f A | Snaga: %.2f W", current_rms, power);

        // Slanje izmjerene snage u Queue za upravljački task
        if (xPowerQueue != NULL) {
            xQueueSend(xPowerQueue, &power, portMAX_DELAY);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    adc_oneshot_del_unit(adc_handle);
    vTaskDelete(NULL);
}

// ── Task 2: Upravljanje relejem i signalizacijom ───────────────────────
static void control_task(void *pvParameters) {
    float power_val = 0.0f;

    while (1) {
        // Provjera je li pritisnuto tipkalo (putem semafora iz ISR-a)
        if (xSemaphoreTake(xButtonSemaphore, 0) == pdTRUE) {
            g_relay_state = !g_relay_state;
            gpio_set_level(GPIO_RELAY, g_relay_state ? 1 : 0);
            gpio_set_level(GPIO_LED, g_relay_state ? 1 : 0);
            ESP_LOGW(TAG, "[TIPKALO] Relej preklopljen na: %s", g_relay_state ? "UKLJUČEN" : "ISKLJUČEN");
        }

        // Čitanje najnovijih podataka o snazi iz Queue-a
        if (xQueueReceive(xPowerQueue, &power_val, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Sigurnosno automatsko isključivanje pri preopterećenju (> 1000 W)
            if (power_val > 1000.0f && g_relay_state) {
                g_relay_state = false;
                gpio_set_level(GPIO_RELAY, 0);
                gpio_set_level(GPIO_LED, 0);
                ESP_LOGE(TAG, "[ZAŠTITA] Preopterećenje! Snaga %.2f W prešla granicu. Relej ISKLJUČEN!", power_val);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    vTaskDelete(NULL);
}

// ── Task 3: Slanje podataka na Zigbee mrežu ─────────────────────────────
static void zigbee_tx_task(void *pvParameters) {
    while (1) {
        ESP_LOGI(TAG, "[ZIGBEE] Slanje statusa čvora na koordinator...");
        // Ovdje se pozivaju funkcije ESP-Zigbee SDK-a za slanje stanja i snage
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    vTaskDelete(NULL);
}

// ── Inicijalizacija Periferije ─────────────────────────────────────────
static void init_hardware(void) {
    // Konfiguracija izlaznih pinova za Relej i LED
    gpio_config_t io_conf_out = {
        .pin_bit_mask = (1ULL << GPIO_RELAY) | (1ULL << GPIO_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_out);

    // Konfiguracija ulaznog pina za Tipkalo s prekidom
    gpio_config_t io_conf_in = {
        .pin_bit_mask = (1ULL << GPIO_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE // Okidanje na pritisak (padajući brid)
    };
    gpio_config(&io_conf_in);

    // Postavljanje ISR servisa za tipkalo
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_BUTTON, gpio_button_isr_handler, NULL);

    // Početna stanja
    gpio_set_level(GPIO_RELAY, 0);
    gpio_set_level(GPIO_LED, 0);
}

// ── Glavna funkcija (app_main) ─────────────────────────────────────────
void app_main(void) {
    ESP_LOGI(TAG, "Pokretanje Pametnog sustava upravljanja potrošnjom energije...");

    // Inicijalizacija NVS memorije
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Stvaranje FreeRTOS mehanizama
    xPowerQueue = xQueueCreate(5, sizeof(float));
    xButtonSemaphore = xSemaphoreCreateBinary();

    // Inicijalizacija hardvera
    init_hardware();

    // Pokretanje FreeRTOS zadaća
    xTaskCreate(adc_measure_task, "adc_task", 4096, NULL, 5, NULL);
    xTaskCreate(control_task,     "ctrl_task", 3072, NULL, 10, NULL);
    xTaskCreate(zigbee_tx_task,   "zgb_task",  4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Sve FreeRTOS zadaće uspješno pokrenute.");
}