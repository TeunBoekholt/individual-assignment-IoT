#include <Arduino.h>
#include <math.h>
#include <arduinoFFT.h>

void vBenchmarkTask(void *pvParameters) {
  uint32_t sampleCount = 0;
  uint32_t startTime = millis();
  
  // Run as fast as possible for exactly 1000 milliseconds
  while (millis() - startTime < 1000) {
    volatile int val = analogReadRaw(4); 
    sampleCount++;
  }
  
  Serial.printf("Hardware Maximum (Software Polling): %lu Hz\n", sampleCount);
  
  vTaskDelete(NULL); 
}

void setup() {
  Serial.begin(115200);
  vTaskDelay(pdMS_TO_TICKS(8000)); 
  xTaskCreatePinnedToCore(vBenchmarkTask, "BenchmarkTask", 4096, NULL, 3, NULL, 1);
}

void loop() {
  vTaskDelete(NULL);
}

// Hardware Maximum (Software Polling): 16482 Hz
