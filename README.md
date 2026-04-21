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
```
<img width="243" height="85" alt="Scherm­afbeelding 2026-04-20 om 18 20 54" src="https://github.com/user-attachments/assets/d6ade2dd-74c4-4fa1-b286-facebd69e2f1" />

**Results & Analysis:**
As you can see both of them consisted of 24 packets (60 seconds / 5 seconds windos * 2 (the ACK response from TCP)), for a total size of 3028 bytes, which is larger than expected. The JSON payload should only be 16 bytes (16 characters), while 3028 / 24 = 126. All these extra bytes are most probably caused by the networking overhead (all the headers).


### 4. End-to-End Latency

I used the `<sys/time.h>` library to synchronize the ESP32's clock with my MacBook. I send a timestamp within the payload and compare it to the time `tshark` receives it. By converting the hex capture to ASCII and subtracting the publish time from the receive time, I get the exact latency.

| Arrival Time (ms) | Decoded JSON Payload | Generation Time (ms) | Latency |
| :--- | :--- | :--- | :--- |
| 1776707392863 | `{"average": 0.01, "ts": 1776707392848}` | 1776707392848 | **15 ms** |
| 1776707397909 | `{"average": 0.01, "ts": 1776707397863}` | 1776707397863 | **46 ms** |
| 1776707402925 | `{"average": 0.01, "ts": 1776707402878}` | 1776707402878 | **47 ms** |
| 1776707407908 | `{"average": 0.00, "ts": 1776707407893}` | 1776707407893 | **15 ms** |
| 1776707412962 | `{"average": -0.03, "ts": 1776707412908}`| 1776707412908 | **54 ms** |

**Conclusion:** The system experiences a network latency ranging between **15 ms and 54 ms**.
