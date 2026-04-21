#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <lmic.h>
#include <hal/hal.h>
#include "arduinoFFT.h"

// ==========================================
// 1. CONFIGURATION & CREDENTIALS
// ==========================================
// Wi-Fi & MQTT
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "YOUR_MQTT_BROKER_IP";

// LoRaWAN (TTN) Credentials (MSB format for LMIC)
static const u1_t PROGMEM APPEUI[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u1_t PROGMEM DEVEUI[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u1_t PROGMEM APPKEY[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8);}
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8);}
void os_getDevKey (u1_t* buf) { memcpy_P(buf, APPKEY, 16);}

// Pin mapping for LoRa module (e.g., RFM95)
const lmic_pinmap lmic_pins = {
    .nss = 18,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 14,
    .dio = {26, 33, 32},
};

// ==========================================
// 2. SYSTEM PARAMETERS & FreeRTOS
// ==========================================
#define MAX_SAMPLING_FREQ 100.0 // Hz
#define WINDOW_SIZE_SEC 5
#define FFT_SAMPLES 128 // Must be a power of 2

// Shared variables
volatile float current_sampling_freq = MAX_SAMPLING_FREQ;
volatile float sample_sum = 0;
volatile int sample_count = 0;

// FreeRTOS Primitives
SemaphoreHandle_t freqMutex;
SemaphoreHandle_t aggMutex;
QueueHandle_t transmissionQueue;

// FFT setup
double vReal[FFT_SAMPLES];
double vImag[FFT_SAMPLES];
arduinoFFT FFT = arduinoFFT(vReal, vImag, FFT_SAMPLES, MAX_SAMPLING_FREQ);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ==========================================
// 3. FreeRTOS TASKS
// ==========================================

// Task 1: Sample the simulated signal
void Task_Sample(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while (1) {
        // Safely read current sampling frequency
        xSemaphoreTake(freqMutex, portMAX_DELAY);
        float freq = current_sampling_freq;
        xSemaphoreGive(freqMutex);

        // Calculate time t in seconds
        float t = millis() / 1000.0;
        
        // Input signal: 2*sin(2*pi*3*t) + 4*sin(2*pi*5*t)
        float value = 2 * sin(2 * PI * 3 * t) + 4 * sin(2 * PI * 5 * t);

        // Add to aggregation window safely
        xSemaphoreTake(aggMutex, portMAX_DELAY);
        sample_sum += value;
        sample_count++;
        xSemaphoreGive(aggMutex);

        // Delay to match sampling frequency
        int delay_ms = 1000 / freq;
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(delay_ms));
    }
}

// Task 2: Compute FFT and adapt frequency
void Task_FFT_Adapt(void *pvParameters) {
    while (1) {
        // Collect samples for FFT at maximum frequency to find true signal peaks
        for (int i = 0; i < FFT_SAMPLES; i++) {
            float t = millis() / 1000.0;
            vReal[i] = 2 * sin(2 * PI * 3 * t) + 4 * sin(2 * PI * 5 * t);
            vImag[i] = 0.0;
            vTaskDelay(pdMS_TO_TICKS(1000 / MAX_SAMPLING_FREQ));
        }

        // Compute FFT
        FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
        FFT.Compute(FFT_FORWARD);
        FFT.ComplexToMagnitude();
        
        // Find peak frequency
        double peakFrequency = FFT.MajorPeak();

        // Adapt frequency: Nyquist theorem (fs >= 2 * fmax). Adding 10% safety margin.
        float new_freq = (peakFrequency * 2.0) * 1.1; 
        if (new_freq > MAX_SAMPLING_FREQ) new_freq = MAX_SAMPLING_FREQ;
        if (new_freq < 1.0) new_freq = 1.0; // Minimum bound

        // Update global frequency safely
        xSemaphoreTake(freqMutex, portMAX_DELAY);
        current_sampling_freq = new_freq;
        xSemaphoreGive(freqMutex);

        Serial.printf("FFT Peak: %.2f Hz. Adapted Sampling Freq: %.2f Hz\n", peakFrequency, new_freq);

        // Run FFT analysis every 10 seconds
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// Task 3: Aggregate data over the time window
void Task_Aggregate(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(WINDOW_SIZE_SEC * 1000));

        xSemaphoreTake(aggMutex, portMAX_DELAY);
        float avg = 0;
        if (sample_count > 0) {
            avg = sample_sum / sample_count;
        }
        // Reset for next window
        sample_sum = 0;
        sample_count = 0;
        xSemaphoreGive(aggMutex);

        Serial.printf("Window Average: %.2f\n", avg);

        // Send to transmission queue
        xQueueSend(transmissionQueue, &avg, portMAX_DELAY);
    }
}

// Task 4: Transmit via MQTT (Edge) & LoRaWAN (Cloud)
void Task_Transmit(void *pvParameters) {
    float aggregate_value;
    
    while (1) {
        // Wait for new aggregated value
        if (xQueueReceive(transmissionQueue, &aggregate_value, portMAX_DELAY) == pdPASS) {
            
            // 1. Send via Wi-Fi / MQTT (Edge Server)
            if (WiFi.status() == WL_CONNECTED) {
                if (!mqttClient.connected()) {
                    mqttClient.connect("ESP32_IoT_Device");
                }
                char payload[20];
                snprintf(payload, sizeof(payload), "%.2f", aggregate_value);
                mqttClient.publish("sensor/aggregate", payload);
                Serial.println("Transmitted to Edge (MQTT)");
            }

            // 2. Send via LoRaWAN (Cloud / TTN)
            // LMIC requires a byte array
            int16_t lora_payload = aggregate_value * 100; // Multiply by 100 to keep 2 decimals
            unsigned char mydata[2];
            mydata[0] = lora_payload >> 8;
            mydata[1] = lora_payload & 0xFF;

            // Check if LMIC is ready to transmit
            if (LMIC.opmode & OP_TXRXPEND) {
                Serial.println(F("LoRa: OP_TXRXPEND, not sending"));
            } else {
                LMIC_setTxData2(1, mydata, sizeof(mydata), 0);
                Serial.println("Transmitted to Cloud (LoRaWAN)");
            }
        }
    }
}

// LMIC Event Handler Boilerplate
void onEvent (ev_t ev) {
    switch(ev) {
        case EV_JOINED:
            Serial.println(F("LoRaWAN Joined!"));
            break;
        case EV_TXCOMPLETE:
            Serial.println(F("LoRaWAN TX Complete"));
            break;
        default:
            break;
    }
}

// Task 5: LoRaWAN OS Loop
// LMIC requires continuous execution of its runloop
void Task_LoRa_Runloop(void *pvParameters) {
    os_init();
    LMIC_reset();
    while (1) {
        os_runloop_once();
        vTaskDelay(pdMS_TO_TICKS(10)); // Yield to other tasks
    }
}

// ==========================================
// 4. MAIN SETUP & LOOP
// ==========================================
void setup() {
    Serial.begin(115200);

    // Initialize Wi-Fi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    mqttClient.setServer(mqtt_server, 1883);

    // Initialize FreeRTOS Primitives
    freqMutex = xSemaphoreCreateMutex();
    aggMutex = xSemaphoreCreateMutex();
    transmissionQueue = xQueueCreate(10, sizeof(float));

    // Create FreeRTOS Tasks
    // xTaskCreate(Function, Name, Stack Size, Params, Priority, Handle)
    xTaskCreate(Task_Sample, "Sample", 2048, NULL, 3, NULL);
    xTaskCreate(Task_FFT_Adapt, "FFT", 4096, NULL, 2, NULL);
    xTaskCreate(Task_Aggregate, "Aggregate", 2048, NULL, 2, NULL);
    xTaskCreate(Task_Transmit, "Transmit", 4096, NULL, 1, NULL);
    xTaskCreate(Task_LoRa_Runloop, "LoRaLoop", 4096, NULL, 4, NULL); // Highest priority for timing
}

void loop() {
    // Empty. FreeRTOS handles execution via Tasks.
}
