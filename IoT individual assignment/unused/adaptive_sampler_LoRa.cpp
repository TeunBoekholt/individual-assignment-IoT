#include <Arduino.h>
#include <math.h>
#include <arduinoFFT.h>
#include <RadioLib.h>

// --- LoRaWAN Credentials ---
uint64_t joinEUI = 0x0000000000000000;
uint64_t devEUI = 0x0000000000000000; // Replace with actual devEUI found on TTN console
uint8_t appKey[] = {}; // Fill with actual appKey found on TTN console
uint8_t nwkKey[] = {}; // Same value as appkey for TTN, because RadioLib uses the same key for both network and application session keys

// --- Heltec Pin Definitions ---
#define NSS 8
#define DIO1 14
#define NRST 12
#define BUSY 13

SX1262 radio = new Module(NSS, DIO1, NRST, BUSY);
LoRaWANNode node(&radio, &EU868);

QueueHandle_t averageQueue;

volatile float currentSignalValue = 0.0;

#define SAMPLES 1024
#define SAMPLING_FREQUENCY 100 // Hz

// Starts at 10ms (1000ms / 100Hz = 10ms), as this is clearly oversamples the 5 Hz signal  
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

void vLoRaTask(void *pvParameters) {
  Serial.println("Initializing LoRa");
  
  int state = radio.begin();

  node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  
  state = node.activateOTAA();

  // Sometimes the session is lost, so a reconnection attempt is needed
  if (state == -1116 || state == -1101) {
    Serial.println("Activation failed or no join accept. Retrying join");
    state = node.activateOTAA();
  }

  float receivedAverage;

  while (1) {
    if (xQueueReceive(averageQueue, &receivedAverage, portMAX_DELAY) == pdPASS) {
      
      uint8_t payload[sizeof(float)];
      memcpy(payload, &receivedAverage, sizeof(float));

      Serial.printf("Sending average: %.2f\n", receivedAverage);
      
      state = node.sendReceive(payload, sizeof(payload), 1);
      
      if (state >= RADIOLIB_ERR_NONE) {
        Serial.println("Payload sent successfully.");
      } else {
        Serial.printf("Transmit failed, code %d\n", state);
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
      if (currentTime - windowStartTime >= 15000) { // Every 15 seconds
        
        // long execStartTime = micros(); // used for calculating per-window execution time

        float windowAverage = sum / count;
        Serial.printf("Average Signal Value over last 15 seconds: %.2f\n", windowAverage);
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
      samplingInterval = pdMS_TO_TICKS(1000 / (2 * highestPrevalentFreq));

      // Update the FFT object with the new sampling frequency
      double samplingFreq = 1000.0 / (pdTICKS_TO_MS(samplingInterval));
      FFT = ArduinoFFT<double>(readyFFTBuffer, (useFirstBuffer ? vImag1 : vImag2), SAMPLES, samplingFreq); 
      
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
  xTaskCreatePinnedToCore(vLoRaTask, "LoRa_Task", 8192, NULL, 1, NULL, 0); 
  xTaskCreatePinnedToCore(TaskFFT, "FFT", 8192, NULL, 1, &FFTTaskHandle, 0);

  // Signal generation and sampling on core 1. They also have a higher priority than the MQTT and FFT tasks 
  // to ensure real-time performance
  xTaskCreatePinnedToCore(vSignalGeneratorTask, "GeneratorTask", 2048, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(vSamplerTask, "SamplerTask", 8192, NULL, 2, NULL, 1);

  
}

void loop() {
  vTaskDelete(NULL);
}
