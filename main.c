/*
 * Projekt: Pametni sustav upravljanja potrošnjom energije temeljen na ESP32-H2[cite: 2]
 * Kolegij: Ugrađeni sustavi[cite: 2]
 * Opis: ACS712 očitavanje struje, izračun radne snage, Zigbee integracija[cite: 2]
 */

#include "Zigbee.h"

// ── Hardverske konfiguracije i GPIO pinovi ─────────────────────────────
#define GPIO_RELAY          4
#define GPIO_LED            8
#define GPIO_BUTTON         9
#define ADC_PIN             1       // GPIO 1 na ESP32-H2[cite: 2]

#define MAINS_VOLTAGE       230.0f  // Mrežni napon (V)[cite: 2]
#define ACS712_SENSITIVITY  0.185f  // Osjetljivost za ACS712-05B (185 mV/A)[cite: 2]
#define ADC_SAMPLES         100     // Broj uzoraka za kalkulaciju RMS-a[cite: 2]
#define ZIGBEE_ENDPOINT     1

ZigbeeLight zbRelay(ZIGBEE_ENDPOINT);
QueueHandle_t xPowerQueue;
SemaphoreHandle_t xButtonSemaphore;
bool g_relay_state = false;

void setRelayState(bool state, const char* source) {
  g_relay_state = state;
  digitalWrite(GPIO_RELAY, state ? HIGH : LOW);
  digitalWrite(GPIO_LED, state ? HIGH : LOW);
  zbRelay.setLight(state); 
  Serial.printf("[RELEJ] Izvor: %s | Stanje: %s\n", source, state ? "UKLJUČEN" : "ISKLJUČEN");
}

void IRAM_ATTR gpio_button_isr_handler() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(xButtonSemaphore, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

void adc_measure_task(void *pvParameters) {
  analogReadResolution(12);
  while (1) {
    long sum_squares = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
      int raw_val = analogRead(ADC_PIN);
      int diff = raw_val - 2047; 
      sum_squares += (diff * diff);
      vTaskDelay(pdMS_TO_TICKS(2));
    }

    float mean_square = (float)sum_squares / ADC_SAMPLES;
    float rms_raw = sqrt(mean_square);
    float voltage_rms = (rms_raw / 4095.0f) * 3.3f; 
    float current_rms = voltage_rms / ACS712_SENSITIVITY;
    current_rms *= 2.0f; // Kompenzacija zbog naponskog djelitelja

    if (current_rms < 0.05f) current_rms = 0.0f;
    float power = MAINS_VOLTAGE * current_rms; // P = U * I (W)[cite: 2]

    Serial.printf("[SENZOR] Struja: %.2f A | Snaga: %.2f W\n", current_rms, power);

    if (xPowerQueue != NULL) {
      xQueueSend(xPowerQueue, &power, portMAX_DELAY);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void control_task(void *pvParameters) {
  float power_val = 0.0f;
  while (1) {
    if (xSemaphoreTake(xButtonSemaphore, 0) == pdTRUE) {
      setRelayState(!g_relay_state, "TIPKALO");
    }

    if (xQueueReceive(xPowerQueue, &power_val, pdMS_TO_TICKS(100)) == pdTRUE) {
      // Sigurnosno automatsko isključivanje pri preopterećenju (> 2500 W)[cite: 2]
      if (power_val > 2500.0f && g_relay_state) {
        setRelayState(false, "ZAŠTITA");
        Serial.printf("[ZAŠTITA] Preopterećenje! Snaga %.2f W prešla granicu. Relej ISKLJUČEN!\n", power_val);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(GPIO_RELAY, OUTPUT);
  pinMode(GPIO_LED, OUTPUT);
  pinMode(GPIO_BUTTON, INPUT_PULLUP);
  digitalWrite(GPIO_RELAY, LOW);
  digitalWrite(GPIO_LED, LOW);

  xPowerQueue = xQueueCreate(5, sizeof(float));
  xButtonSemaphore = xSemaphoreCreateBinary();
  attachInterrupt(digitalPinToInterrupt(GPIO_BUTTON), gpio_button_isr_handler, FALLING);

  zbRelay.setManufacturerAndModel("Espressif", "SmartPlugH2");
  zbRelay.onLightChange([](bool state) {
    Serial.printf("[ZIGBEE] Naredba iz mreže: %s\n", state ? "UKLJUČI" : "ISKLJUČI");
    g_relay_state = state;
    digitalWrite(GPIO_RELAY, state ? HIGH : LOW);
    digitalWrite(GPIO_LED, state ? HIGH : LOW);
  });

  Zigbee.addEndpoint(&zbRelay);
  Zigbee.begin();

  xTaskCreate(adc_measure_task, "adc_task", 4096, NULL, 5, NULL);
  xTaskCreate(control_task, "ctrl_task", 3072, NULL, 10, NULL);
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
