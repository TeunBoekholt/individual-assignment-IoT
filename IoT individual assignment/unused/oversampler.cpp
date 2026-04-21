#include <Arduino.h>
#include <math.h>
#include <arduinoFFT.h>

volatile float currentSignalValue = 0.0;

#define SAMPLING_FREQUENCY 1000 // Reduced for stability during testing

// The adaptive sampling delay for the IoT device.
// Starts at 1ms (1000ms / 1000Hz = 1ms), as this is clearly oversamples the 4 Hz signal  
TickType_t samplingDelayTicks = pdMS_TO_TICKS(1000 / SAMPLING_FREQUENCY); // Initial delay based on the sampling frequency
TickType_t generationInterval = pdMS_TO_TICKS(1); // Generates a signal value every 1 ms

// Signal Generator
void vSignalGeneratorTask(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();

  while (1) { 
    float t = micros() / 1000000.0;
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
    
    float sampledData = currentSignalValue;

    Serial.print(">Signal:");
    Serial.println(sampledData);

    i++;
    
    vTaskDelayUntil(&xLastWakeTime, samplingDelayTicks);
  }
}

void setup() {
  Serial.begin(115200);

  xTaskCreatePinnedToCore(vSignalGeneratorTask, "GeneratorTask", 2048, NULL, 3, NULL, 1);

  xTaskCreatePinnedToCore(vSamplerTask, "SamplerTask", 8192, NULL, 2, NULL, 1);
}

void loop() {
  vTaskDelete(NULL);
}