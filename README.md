# System Performance & Process Documentation
Teun Boekholt

## Process Documentation
Initially, I attempted to manage everything within the `IRAM_ATTR` without using RTOS tasks. When this approach proved unsuccessful, I transitioned to the dual ESP32 setup described in class and on GitHub. However, due to limited hardware experience, I encountered issues with this configuration.

Ultimately, I settled on a **single ESP32 using FreeRTOS tasks**. This architecture yielded relatively good results for generating the signal, calculating the FFT, adapting the sampling rate, and averaging the signal over a 5-second window.

---

## Performance Measurements

### 1. Energy Savings
* **Oversampled Signal (100 Hz):** Consistent 60 mA usage.
* **Adaptive Sampler (10 Hz):** `[Insert Value here]` mA. 

### 2. Per-Window Execution Time
I measured the execution time by recording the timestamp before the window execution (averaging and transmitting) and when it completed.

**Raw Outputs:**
> 282 µs, 249 µs, 261 µs, 282 µs, 294 µs, 267 µs, 262 µs, 261 µs, 268 µs, 263 µs

**Result:** For these 10 windows, the average execution time is **268.9 microseconds** (0.2689 milliseconds).

### 3. Volume of Data Transmitted
To capture the transmission data over Wi-Fi for 60 seconds (for both the oversampled and adapted signals), I used the following `tshark` command:
```bash
sudo tshark -i en0 -f "host [IP] and port 1883" -a duration:60 -w [NAME].pcap
