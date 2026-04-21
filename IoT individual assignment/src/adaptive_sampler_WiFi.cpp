#include <Arduino.h>
#include <math.h>
#include <arduinoFFT.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <sys/time.h>

// --- Networking Credentials ---
const char* ssid = "Wind3 HUB-7D05D0";
const char* password = "32wxbpndedmzjsi9";
const char* mqtt_server = "192.168.1.49"; 

// –––– Commands to run in terminal –––––:
// mosquitto_sub -h localhost -t v1/devices/me/telemetry
// mosquitto -c mosquitto.conf


// Acces token
const char* thingsboard_token = "ODXliL6WvN1aEZEHIDLX"; 

WiFiClient espClient;
PubSubClient mqttClient(espClient);
QueueHandle_t averageQueue;

volatile float currentSignalValue = 0.0;

#define SAMPLES 1024 // Size of FFT buffer
#define SAMPLING_FREQUENCY 1000 // Hz

// Starts at 1ms (1000ms / 1000Hz = 1ms), as this is clearly oversamples the 5 Hz signal  
TickType_t samplingInterval = pdMS_TO_TICKS(1000 / SAMPLING_FREQUENCY); // Initial delay based on the sampling frequency
TickType_t generationInterval = pdMS_TO_TICKS(1); // Generates a signal value every 1 ms


double vReal1[SAMPLES];
double vImag1[SAMPLES];
double vReal2[SAMPLES];
double vImag2[SAMPLES];
bool useFirstBuffer = true;
double* readyFFTBuffer;

TaskHandle_t FFTTaskHandle = NULL;
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal1, vImag1, SAMPLES, SAMPLING_FREQUENCY);


void vMQTTTask(void *pvParameters) {
  mqttClient.setServer(mqtt_server, 1883); 
  float receivedAverage;

  while (1) {
    if (xQueueReceive(averageQueue, &receivedAverage, portMAX_DELAY) == pdPASS) {
      Serial.println(WiFi.localIP());

      // Connect to Wi-Fi
      if (WiFi.status() != WL_CONNECTED) {
        Serial.print("connecting to wifi");
        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED) {
          vTaskDelay(pdMS_TO_TICKS(500));
        }
        Serial.println(" Connected");
        configTime(0, 0, "pool.ntp.org");
      }

      // Connect to Mosquitto MQTT Broker
      if (!mqttClient.connected()) {
        Serial.print("Connecting to Mosquitto");
        
        if (mqttClient.connect("ESP32_Heltec")) {
          Serial.println(" Connected");
        } else {
          Serial.print(" Failed, rc=");
          Serial.println(mqttClient.state());
        }
      }

      // Send the Data
      if (mqttClient.connected()) {

        // Aliging the timestamp with the moment of data generation by getting the current time right before publishing
        struct timeval tv;
        gettimeofday(&tv, NULL);
        long long current_time_ms = (long long)tv.tv_sec * 1000LL + (long long)tv.tv_usec / 1000LL;

        char jsonPayload[80];
        snprintf(jsonPayload, sizeof(jsonPayload), "{\"average\": %.2f, \"ts\": %lld}", receivedAverage, current_time_ms);
        
        mqttClient.publish("v1/devices/me/telemetry", jsonPayload);
        
        Serial.printf("Published: %s\n", jsonPayload);
      }
    }
  }
}

// Signal Generator
void vSignalGeneratorTask(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();

  while (1) { 
    double t = micros() / 1000000.0;
    currentSignalValue = 2 * sin(2 * PI * 3 * t) + 4 * sin(2 * PI * 5 * t);
    vTaskDelayUntil(&xLastWakeTime, generationInterval);
  }
}

// Signal Sampler
void vSamplerTask(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  
  int i = 0;

  long windowStartTime = millis();
  float sum = 0.0;
  int count = 0;

  while (1) {
    if (i < SAMPLES) {
      float sampledData = currentSignalValue;

      // ––––– Update the sum and count for the window calculation –––––––
      sum += sampledData;
      count++;
      long currentTime = millis();
      if (currentTime - windowStartTime >= 5000) { // Every 5 seconds
        
        // long execStartTime = micros(); // used for calculating per-window execution time

        float windowAverage = sum / count;
        Serial.printf("Average Signal Value over last 5 seconds: %.2f\n", windowAverage);
        // Send the average to the MQTT task via the queue
        xQueueSend(averageQueue, &windowAverage, portMAX_DELAY);
        sum = 0.0;
        count = 0;
        
        // UCOMMENT the following lines for measuring the execution time of each window processing
        // long execEndTime = micros();
        // Serial.printf("Window execution time: %ld microseconds\n", execEndTime - execStartTime);
        
        windowStartTime = currentTime;
      }
      // ––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––

      if (useFirstBuffer) {
        vReal1[i] = sampledData;
        vImag1[i] = 0;
      } else {
        vReal2[i] = sampledData;
        vImag2[i] = 0;
      }

      // Serial.print(">Signal:");
      // Serial.println(sampledData);

      i++;
      
    } else if (i >= SAMPLES) { // Buffer is full

      if (useFirstBuffer) {
        FFT.setArrays(vReal1, vImag1, SAMPLES);
        readyFFTBuffer = vReal1;
      } else {
        FFT.setArrays(vReal2, vImag2, SAMPLES);
        readyFFTBuffer = vReal2;
      }
      
      xTaskNotifyGive(FFTTaskHandle);
      vTaskDelay(pdMS_TO_TICKS(10)); // Short delay to ensure the FFT task has time to process the data
      
      useFirstBuffer = !useFirstBuffer; // Switch buffers for the next round of sampling
      i = 0; 
    }
    
    vTaskDelayUntil(&xLastWakeTime, samplingInterval);
  }
}

void TaskFFT(void *pvParameters) {
  while (1) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.compute(FFT_FORWARD);
    FFT.complexToMagnitude();

    double currentSamplingFreq = 1000.0 / pdTICKS_TO_MS(samplingInterval);

    double maxAmplitude = 0;
    // readyFFTBuffer contains the magnitudes after calling complexToMagnitude()
    // So we can use it to find the maximum amplitude in the spectrum
    for (int i = 1; i < (SAMPLES / 2); i++) { 
      
      if (readyFFTBuffer[i] > maxAmplitude) {
        maxAmplitude = readyFFTBuffer[i];
      }
      
    }

    // A threshold to identify significant frequencies using the max amplitude 
    double threshold = maxAmplitude * 0.15; 
    double highestPrevalentFreq = 0.0;
    
    // Go backwards through the spectrum to find the highest prevalent frequency above the threshold
    for (int i = (SAMPLES / 2) - 1; i > 0; i--) {
      if (readyFFTBuffer[i] > threshold) {
        highestPrevalentFreq = i * (currentSamplingFreq / (double)SAMPLES);
        break; 
      }
    }
    if (highestPrevalentFreq > 0.0) { // Safety check to avoid division by zero 
      // COMMENT the following line to stop the adaptive sampling and keep the initial sampling frequency (for measuring)
      samplingInterval = pdMS_TO_TICKS(1000 / (2 * highestPrevalentFreq));
      Serial.printf("Highest Prevalent Freq: %.2f Hz\n", highestPrevalentFreq);
    } else {
      Serial.println("No prevalent frequency found. Keeping current interval.");
    }
  }
}

void setup() {
  Serial.begin(115200);

  averageQueue = xQueueCreate(10, sizeof(float));

  // MQTT and FFT on core 0 to keep them away from the higher priority real-time tasks
  // FFT needs high stack size to to handle the large arrrays
  xTaskCreatePinnedToCore(vMQTTTask, "MQTT_Task", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskFFT, "FFT", 8192, NULL, 1, &FFTTaskHandle, 0);

  // Signal generation and sampling on core 1. They also have a higher priority than the MQTT and FFT tasks 
  // to ensure real-time performance
  xTaskCreatePinnedToCore(vSignalGeneratorTask, "GeneratorTask", 2048, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(vSamplerTask, "SamplerTask", 8192, NULL, 2, NULL, 1);
}

void loop() {
  vTaskDelete(NULL);
}
