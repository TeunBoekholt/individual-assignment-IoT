PROCESS DOCUMENTATION

First I tried to do everything in the IRAM_ATTR and not with RTOS tasks. This didn't really work out, so I switched to 
using the two ESP32 setup as described in class and on the github. However, I ran into issues (I think because I am not 
that experienced with using hardware). After this I settled on using a single ESP32 with RTOS tasks, as this setup seemed
to generated relatively okay looking results for generating a signal, calculating the FFT, adapting the sampling rate and 
then averaging the signal over a 5 second window.


PERFORMANCE MEASUREMENTS

Engergy Savings:

1.  oversampled signal energy consumption (100 Hz): consistent 60 mA usage 
    adaptive sampler energy consumption (10 Hz): 

2.  Per-window execution time: I saved the time before the window execution (averaging and transmitting)
    and when it's done to get the per-window execution time. Here are some outputs:
    Window execution time: 282 microseconds
    Window execution time: 249 microseconds
    Window execution time: 261 microseconds
    Window execution time: 282 microseconds
    Window execution time: 294 microseconds
    Window execution time: 267 microseconds
    Window execution time: 262 microseconds
    Window execution time: 261 microseconds
    Window execution time: 268 microseconds
    Window execution time: 263 microseconds

    So for these 10 windows that's an average of 268.9 microseconds, or 0.2689 miliseconds.
    
3. Volume of data transmitted: I ran the following tshark command to capture the transmission data over WiFi for 60 seconds,
   both for the oversampled signal and the adapted signal: sudo tshark -i en0 -f "host [IP] and port 1883" -a duration:60 -w [NAME].pcap 
   As you can see both of them consisted of 24 packets (60 seconds / 5 seconds windos * 2 (the ACK response from TCP)), for a total size of 3028 bytes, which is larger than expected. The JSON payload should only be 16 bytes (16 characters), while 3028 / 24 = 126. All these extra bytes are most probably caused by the networking overhead (all the headers). 

4. End-to-End Latency: For measuring the end-to-end latency of the system I used the sys/time.h library to synchronise the ESP32 with the same time as my macbook. We then send a timestamp with the payload and compare it to the time tshark receives it. Then we just have to convert the hex code to ASCII and subtract the publish time from the receive time to get:


1776707392863  - {"average": 0.01, "ts": 1776707392848} = 25ms
1776707397909  - {"average": 0.01, "ts": 1776707397863} = 46ms
1776707402925  - {"average": 0.01, "ts": 1776707402878} = 47ms
1776707407908  - {"average": 0.00, "ts": 1776707407893} = 15ms
1776707412962  - {"average": -0.03, "ts": 1776707412908} = 54ms

So a latency between 20ms - 50ms.
