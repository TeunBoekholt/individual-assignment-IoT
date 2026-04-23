
# Inidividual Assignment – Internet-of-Things
Teun Boekholt, 2284223
boekholt.2284223@studenti.uniroma1.it


## Walk-through of the system 
Before running any code you need to have one ESP32 LoRa Heltec V3 for doing the generation of the signal, sampling and the the transmission over WiFi and LoRa. To measure energy you also need an INA219 and another ESP32 dev board.

The set-up steps:
1. Clone the repository and put all the code in the `src` folder in a new platformio project (I used VS code as an IDE)
2. The `unused` folder contains different programs to put on the ESP32 depending on what you want to test:
  * The `energy_measurer` is solely used to measure the energy with another ESP32 for the first question. The code was not written by me but taken from https://andreavitaletti.github.io/IoT_short_course/energy/.
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

My chosen signal (actually just the same as the example one in the assignment):

``` bash
2 * sin(2 * PI * 3 * t) + 4 * sin(2 * PI * 5 * t)
```

### Architecture Diagram

``` mermaid
%%{init: {'theme':'dark', 'themeVariables': { 'edgeLabelBackground':'#0d1117', 'primaryTextColor': '#c9d1d9', 'lineColor': '#8b949e'}}}%%
graph TB
    %% Definitions of outside entities
    subgraph Core1 [CORE 1: Hard Real-Time]
        direction TB
        GenTask[vSignalGeneratorTask<br/>Priority: 3 Highest<br/>Period: 1 ms]
        SamplerTask[vSamplerTask<br/>Priority: 2<br/>Period: Adaptive]
    end

    subgraph Core0 [CORE 0: Processing & Comms]
        direction TB
        FFTTask[TaskFFT<br/>Priority: 1]
        MQTTTask[vMQTTTask<br/>Priority: 1]
    end

    %% Internal Data Entities
    SignalVar((currentSignalValue))
    IntervalVar((samplingInterval))
    Queue[[averageQueue<br/>Size: 10]]
    Notify((Task<br/>Notification))

    subgraph Memory [Dual-Core Shared Memory]
        subgraph PingPong [Ping-Pong Buffers]
            Buf1[vReal1 / vImag1]
            Buf2[vReal2 / vImag2]
        end
        ReadyPtr((readyFFTBuffer))
    end

    %% -- Connections & Data Flow --

    %% Core 1 Internal
    GenTask -->|Writes| SignalVar
    SignalVar -->|Reads| SamplerTask

    %% IPC: Sampler to MQTT
    SamplerTask -->|Sends 5s Average| Queue
    Queue -->|Receives| MQTTTask

    %% Memory Management by Sampler
    SamplerTask -->|Fills A| Buf1
    SamplerTask -->|Fills B| Buf2
    SamplerTask -->|Swaps Pointer| ReadyPtr

    %% IPC: Sampler to FFT (Synchronization)
    SamplerTask -->|Triggers| Notify
    Notify -->|Wakes| FFTTask

    %% FFT Data Access
    ReadyPtr -.->|Points to Ready Data| FFTTask

    %% Adaptive Feedback Loop
    FFTTask -->|Updates| IntervalVar
    IntervalVar -->|Controls Speed| SamplerTask

    %% MQTT Output
    MQTTTask -->|Publishes JSON| ThingsBoard[ThingsBoard<br/>via Mosquitto]

    %% Styling optimized for GitHub Dark Mode
    classDef task fill:#1f6feb22,stroke:#1f6feb,stroke-width:2px,color:#c9d1d9,rx:5,ry:5;
    classDef var fill:#d2992222,stroke:#d29922,stroke-width:2px,stroke-dasharray: 5 5,color:#c9d1d9;
    classDef queue fill:#23863622,stroke:#2ea043,stroke-width:2px,color:#c9d1d9;
    classDef mem fill:#21262d,stroke:#30363d,stroke-width:2px,color:#c9d1d9;
    classDef external fill:#8957e522,stroke:#8957e5,stroke-width:2px,stroke-dasharray: 5 5,color:#c9d1d9;

    class GenTask,SamplerTask,FFTTask,MQTTTask task;
    class SignalVar,IntervalVar,Notify,ReadyPtr var;
    class Queue queue;
    class Buf1,Buf2,Memory,PingPong mem;
    class ThingsBoard external;

    %% Make all arrows broader and visible against dark grey
    linkStyle default stroke-width:3px,stroke:#8b949e,color:#c9d1d9;
```

### Maximum Sampling Frequency
Since I am using the FreeRTOS I at first didn't know how to get to a sampling frequency above 1000Hz, seeing as the tick rate of FreeRTOS was configured on 1 tick = 1ms. In thend I managed to write a function that just read an analog pin (4) and incremented a counter every time it did so, resulting in a smapling frequency of `16.48KHz`. This is not the actual hardware limit, but for all purposes and signals considered for this project this maximum sampling frequency was more than sufficient – for all testing (except for one of the bonus signals) it was even enough to just start out sampling at 100Hz. 

### Identify optimal sampling frequency

Identifying the optimal sampling frequency was achieved by letting the a Sampler Task fill up an FFT buffer of size 1024 (which means we have a bin resolution of just under 1Hz). Once the buffer is full I used the `xTaskNotifyGive()` function to activate the FFT task, which first calculatest the most prevalent frequency, which it used to set a treshold (40% of the maximum frequency). Then we loop from back to front over all the bins to select the highest frequency which reaches the threshold. This frequency gets multiplied by 2 to get the Nyquist Freqenyc, which becomes the next sampling frequency.

To prevent the Sampler Task writing values in the buffer while the FFT task is analyzing it, I used two (ping-pong) buffers which get swapped around.

### Compute aggregate function over a window

To compute the aggregate function over a 5-second window I used the following function inside the Sampler Task.

``` bash
sum += sampledData;
count++;
long currentTime = millis();
if (currentTime - windowStartTime >= 5000) { // Every 5 seconds
  
  float windowAverage = sum / count;
  
  Serial.printf("Average Signal Value over last 5 seconds: %.2f\n", windowAverage);
  // Send the average to the MQTT task via the queue
  xQueueSend(averageQueue, &windowAverage, portMAX_DELAY);
  sum = 0.0;
  count = 0;
  
  windowStartTime = currentTime;
}
```

### Communicate the aggregate value to the nearby server

### Communicate the aggregate value to the cloud

<img width="1130" height="362" alt="Scherm­afbeelding 2026-04-20 om 16 36 09" src="https://github.com/user-attachments/assets/edb2c289-6d03-4e22-b5a1-941658d349a9" />



---

## Performance Measurements

### 1. Energy Savings
Below is a diagram of my energy measuring setup.

<img width="671" height="384" alt="Scherm­afbeelding 2026-04-21 om 18 42 36" src="https://github.com/user-attachments/assets/33ba6b6a-7677-4121-8b3d-c74490d432b8" />

Since I am running everything on my macbook I couldn't use the Serial Plotter, that's why I used the VScode extention 'Teleplot' to visualize the energy measurements. The results over a 5-second window:

* **Oversampled Signal (1000 Hz):** Base 45 mA usage with energy spikes very often.
<img width="568" height="431" alt="oversampled" src="https://github.com/user-attachments/assets/843cbd5a-d4fe-4b55-b0dc-94a39d397db2" />
As you can see in the image above the ESP32 almost never rests with the oversampling setup, the energy consumption is extremely jittery. This is mainly caused be the fact that the FreeRTOS scheduler has to manage two tasks that wake up 1000 times a second, which gives a lot of system overhead. 


* **Adaptive Sampler (10 Hz):** Consistent 45 mA usage with spikes for WiFi transmitting.
<img width="559" height="384" alt="adaptive" src="https://github.com/user-attachments/assets/41ba56c9-a5c9-4d19-a001-3899d5a3e290" />
The fact that I see a negligible difference between the oversampled and adapted sampled BASE signal actually makes sense for my implementation. Significant energy savings could only be achieved by having the ESP32 enter sleep mode between performing its sampling tasks. When sampling at 10Hz (which would be optimal for my chosen signal) the system in theory has enough time to enter and exit sleep mode before having to take its next sample, thus saving energy.

However, in my implementation the generation of the signal is also a FreeRTOS task which occurs every ms. Therefore the system won't enter sleep mode, as it notices that the latency of the sleep policy would be too high. 

If one were to actually prioritize saving energy, in this case it would be possible to move the generation of the signal within the Sampling taks, as the signal value is used nowhere else. In this way the generation function doesn't have to be called every miliseconds and the system has time to enter and exit sleep mode.

Lastly, the higher energy spikes in this implementation are likely caused by the more computationally heavy calling of the WiFi transmitting task and the FFT computation. A good sign is that at least you can see that the spikes are less frequent than for the oversampled implementation.

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

## Bonus – testing three different signals

### 1. Standard signal 

``` bash
2 * sin(2 * PI * 3 * t) + 4 * sin(2 * PI * 5 * t)
```

Below you can see the resulting graph of 10 second measurement of the standard signal using the adaptive sampling technique, which settled on a nyquist frequency of approximately 10 Hz.

<img width="612" height="332" alt="Scherm­afbeelding 2026-04-22 om 20 41 25" src="https://github.com/user-attachments/assets/ef2e049f-b7d6-42db-b4bd-07511dc43ff5" />

As you can see it is not a perfect representation of the original signal, as there are certain intervals for which the signal appears to get sampled quite noisily. However, the calculation of the average signal value was not hinered by this fact in this case. Still, seeing as the Nyquist frequency is a bare minimum sampling frequency, it might be good practice to up the actual sampling frequency to about 2.5x the highest prevalent frequency to get a better estimate.

### 2. High-frequency signal 

``` bash
3 * sin(2 * PI * 200 * t)
```

When testing my code on a higher frequency signal (in this case 200 Hz) I noticed that at first the FFT kept estimating the highest prevalent frequency to be too low, leading to a gradual underestimation of the optimal sampling frequency. I investigated multiple possible causes for this underestimation, rangnig from checking my FFT bin resolutions to making sure the tick rate of my FreeRTOS was actually set to 1ms. In the end the issue was quite trivial, but here is what it was and how I fixed it:
* After checking the magnitude values of the different frequency bins after the FFT was done I noticed that for the first FFT calculation there was quite a large number in the final bin (corresponding to 505Hz), probably due to a noise spike
* My code then calculates the new sampling rate as follows: `samplingInterval = pdMS_TO_TICKS(1000.0 / (2.0 * 505))`, which leads to a function call `pdMS_TO_TICKS(0.99)`, which leads to C++ chopping off the decimal and an interval of 0 ticks
* Because of this the sampler task is called at the CPU limit with no 1ms delay, leading to the buffer filling up in less than 1000ms even though the FFT function still expects a full buffer to have been collected over this timespan. Following this the new highest prevelant frequency gets calculated to be way too low and we enter the aliassing spiral.
* **The fix** to this problem turned out to be pretty simple: just upping the threshold required to be selected as a **prevalent** frequency from 0.15 to 0.40 did the trick. Now the noise spike in the 505Hz bin wasn't seen as a prevalent frequency and everyhing worked as it should. The plot below shows the sampling over 1 second.

<img width="482" height="408" alt="Scherm­afbeelding 2026-04-22 om 21 38 14" src="https://github.com/user-attachments/assets/88c70ce9-4c64-4b55-88de-a8a5517d5350" />

### 3. Varying-frequency signal

As a final test I implemented a signal that changed its frequency every 10 seconds from 5 Hz to 40 Hz and the other way around after another 10 seconds. As expected, my program had trouble correctly sampling this signal. As soon as the FFT calculates a highest prevalent frequency of 5 Hz (which is correct for the first signal) the sampling rate gets adjusted to 10Hz. Unfortunately this is not high enough to correctly sample the 40 Hz signal, resulting in some aliasing (as can be seen in the graph below).

<img width="487" height="373" alt="Scherm­afbeelding 2026-04-22 om 21 45 51" src="https://github.com/user-attachments/assets/f44f12b5-a729-415a-80b9-2333756d84b3" />

To fix this issue, a safety feature should be implemented where, after every so-many seconds, the system samples again at a high frequency. If we then discover a higher prevalent frequency than before we could either keep a new high sampling frequency or keep the adaptive sampling mechanism, also allowing for lowering the sampling freuquency again later on. 

In the end I unfortunately did not have the time to implement this safety net, however. The same goes for the Filter bonus. 

---

## LLM Performance

I have attached a `LLM.cpp` file which is the result of me basically copying the assingment in Gemini and asking it to write a C++ script to perform everything. Some intersting things I noted:
* Probably the largest shortcoming of the LLM is that it doesn't seme to have understood correctly what the desired frequency is. Gemini just takes the `MajorPeak()` function from the `FFT` library and then determines this is the highest prevalent frequency (which we know it is not, it's just the MOST prevalent frequency). This could potentially lead to severe undersampling of the signal if the most prevalent frequency is much lower than the highest prevalent frequency. 
* The LLM gives the LoRa task highest priority because of 'timing'. In my implementation I purposefully didn't do this because I want to always prioritize generating and sampling the signal. To me it seems acceptable to have the LoRa transmission be a little late, though we don't want to miss any samples.
* The LLM uses LMIC, which (as I understand it) is a library used for briding the gap between LoRa (the physical hardware) and LoRaWAN (the networking protocol)
* The LLM adds a 10% safety margin to the Nyquist Frequency.
* The LLM uses mutexes, which it explained – after I prompted it some more – are basically keys used for the synchronous used of variables to prevent (for example) one task from writing variables that are currently being read. This actually seems like very useful functionality, and in hindsight I will look into for designing the FreeRTOS pipeline for our group project.
* The LLM doesn't specify for each task what core to use. It explained it did so as to 
