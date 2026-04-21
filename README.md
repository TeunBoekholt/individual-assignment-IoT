
# Inidividual Assignment – Internet-of-Things
Teun Boekholt, 2284223
boekholt.2284223@studenti.uniroma1.it


## Walk-through of the system 
Before running any code you need to have one ESP32 LoRa Heltec V3 for doing the generation of the signal, sampling and the the transmission over WiFi and LoRa. To measure energy you also need an INA219 and another ESP32 dev board.

The set-up steps:
1. Clone the repository and put all the code in the `src` folder in a new platformio project (I used VS code as an IDE)
2. The `unused` folder contains different programs to put on the ESP32 depending on what you want to test:
  * The `energy_measurer` is solely used to measure the energy with another ESP32 for the first question. The cod was not written by me but taken from https://andreavitaletti.github.io/IoT_short_course/energy/.
  * The `oversampler` is used to just sample the generated signal at a frequency of 1000Hz. This one was used to measure energy consumption and data volume
  * The two `adaptive_sampler` files are used for computing the FFT and adjusting the sampling rate, after which one of them sends it to a local edge server over WiFi, and the other sends it with LoRa to the TTN cloud
3. For almost any test purposes the `adaptive_sampler_WiFi.cpp` should be enough, so loading this one on an ESP32 is the recommended course of action
4. For getting certain measurements, the section on Performance Measurements describes what commands to use to get them.
5. For using WiFi, make sure to have Mosquitto MQTT broker installed and to put in your WiFi credentials and edge server IP at the top of the code. To listen to incoming messages from the ESP32 use the following command (on mac):
``` bash
mosquitto_sub -h localhost -t v1/devices/me/telemetry
```
6. For using LoRa, make sure to have your ESP32 registered as a device in the TTN console. For my code it has to be an OTAA device, meaning you should see an AppKey and a DevEUI key in the overview like shown below. These should be put into the fields of my code. To succesfully have the LoRa payload be picked up, the ESP32 needs to be in range of a TTN gateway.

<img width="522" height="295" alt="Scherm­afbeelding 2026-04-21 om 11 07 45" src="https://github.com/user-attachments/assets/30a2dc31-077c-4d5a-9f32-7608c05160ee" />

<img width="488" height="182" alt="Scherm­afbeelding 2026-04-21 om 11 07 56" src="https://github.com/user-attachments/assets/3f8aa98e-5d99-4562-a40b-f04fb19aa47e" />


## Process Documentation
Initially, I attempted to manage everything within the `IRAM_ATTR` without using RTOS tasks. When this approach proved unsuccessful, I transitioned to the dual ESP32 setup described in class and on GitHub. However, due to limited hardware experience, I encountered issues with this configuration.

Ultimately, I settled on a **single ESP32 using FreeRTOS tasks**. This architecture yielded relatively good results for generating the signal, calculating the FFT, adapting the sampling rate, and averaging the signal over a 5-second window. The only problems with this implementation were the inability to get energy savings by going into sleep mode and the fact that FreeRTOS ticks are 1ms, meaning it's not possible with the tasks to sample at more than 1000Hz. This is why the highest frequecny you will encounter in my implemenation is 1000Hz, which still significantly oversamples my chosen signal, but doesn't come close to the actual maximum sampling frequency of the device.

My chosen signal:

``` bash
2 * sin(2 * PI * 3 * t) + 4 * sin(2 * PI * 5 * t)
```

---

## Performance Measurements

### 1. Energy Savings
Since I am running everything on my macbook I couldn't use the Serial Plotter, that's why I used the VScode extention 'Teleplot' to visualize the energy measurements. The results:

* **Oversampled Signal (1000 Hz):** Consistent 60 mA usage.
* **Adaptive Sampler (10 Hz):** Consistent 60 mA usage with spikes for WiFi transmitting.

The fact that I see a negligible difference between the oversampled and adapted sampled signal actually makes sense for my implementation. Significant energy savings could only be achieved by having the ESP32 enter sleep mode between performing its sampling tasks. When sampling at 10Hz (which would be optimal for my chosen signal) the system in theory has enough time to enter and exit sleep mode before having to take its next sample, thus saving energy.

However, in my implementation the generation of the signal is also a FreeRTOS task which occurs every ms. Therefore the system won't enter sleep mode, as it notices that the latency of the sleep policy would be too high. 

If one were to actually prioritize saving energy, in this case it would be possible to move the generation of the signal within the Sampling taks, as the signal value is used nowhere else. In this way the generation function doesn't have to be called every miliseconds and the system has time to enter and exit sleep mode.


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

I used the `<sys/time.h>` library to synchronize the ESP32's clock with my MacBook. I send a timestamp within the payload and compare it to the time `tshark` receives it. The following tshark command listens on port 1883 and makes it easy to compare the 'received' timestamp with the 'published' timestamp:

``` bash
sudo tshark -i en0 -f "host [IP] and port 1883" -Y "mqtt.msg" -T fields -e frame.time_epoch -e mqtt.msg
```
Screenshot of some results:

<img width="619" height="204" alt="Scherm­afbeelding 2026-04-20 om 19 54 43" src="https://github.com/user-attachments/assets/cc39e1a2-3281-492f-88ed-6c8be666cfa4" />

By converting the hex capture to ASCII and subtracting the publish time from the receive time, I get the exact latency.


| Arrival Time (ms) | Decoded JSON Payload | Generation Time (ms) | Latency |
| :--- | :--- | :--- | :--- |
| 1776707392863 | `{"average": 0.01, "ts": 1776707392848}` | 1776707392848 | **15 ms** |
| 1776707397909 | `{"average": 0.01, "ts": 1776707397863}` | 1776707397863 | **46 ms** |
| 1776707402925 | `{"average": 0.01, "ts": 1776707402878}` | 1776707402878 | **47 ms** |
| 1776707407908 | `{"average": 0.00, "ts": 1776707407893}` | 1776707407893 | **15 ms** |
| 1776707412962 | `{"average": -0.03, "ts": 1776707412908}`| 1776707412908 | **54 ms** |

**Conclusion:** The system experiences a network latency ranging between **15 ms and 54 ms**.


---

## LLM Performance

I have attached a `LLM.cpp` file which is the result of me basically copying the assingment in Gemini and asking it to write a C++ script to perform everything. Some intersting things I noted:
* Probably the largest shortcoming of the LLM is that it doesn't seme to have understood correctly what the desired frequency is. Gemini just takes the `MajorPeak()` function from the `FFT` library and then determines this is the highest prevalent frequency (which we know it is not, it's just the MOST prevalent frequency). This could potentially lead to severe undersampling of the signal if the most prevalent frequency is much lower than the highest prevalent frequency. 
* The LLM gives the LoRa task highest priority because of 'timing'. In my implementation I purposefully didn't do this because I want to always prioritize generating and sampling the signal. To me it seems acceptable to have the LoRa transmission be a little late, though we don't want to miss any samples.
* The LLM uses LMIC, which (as I understand it) is a library used for briding the gap between LoRa (the physical hardware) and LoRaWAN (the networking protocol)
* The LLM adds a 10% safety margin to the Nyquist Frequency.
* The LLM uses mutexes, which it explained – after I prompted it some more – are basically keys used for the synchronous used of variables to prevent (for example) one task from writing variables that are currently being read. This actually seems like very useful functionality, and in hindsight I will look into for designing the FreeRTOS pipeline for our group project.
* The LLM doesn't specify for each task what core to use. It explained it did so as to 
