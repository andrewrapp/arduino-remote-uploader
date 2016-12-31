# arduino-remote-uploader

This project provides remote upload of Arduino sketches over Wifi (ESP8266), XBee, and Nordic nRF24L01+. For more information see https://medium.com/@nowir3s/arduino-remote-uploader-aa61e4d620b1

<a href="https://vine.co/v/O0jLBw7aO1m" target="_blank"><img src="https://raw.githubusercontent.com/andrewrapp/arduino-remote-uploader/master/resources/vine-xbee.png" alt="vine" width="300" height="299" border="0" /></a>

In this Vine we first see the remote XBee Arduino blinking fast (every 50ms). Then, the sketch is updated to blink slower (every 1 second) and compiled. Next, the compiled sketch is transferred to the remote Arduino, via XBee, and flashed onto the secondary Arduino. The command completes and indicates that the flash was successful. And lastly we see the Arduino is running the new sketch and blinking slowly. So there it is: over the air Arduino programming via XBee in six seconds!

<a href="https://vine.co/v/empxvZgpuqV" target="_blank"><img src="https://raw.githubusercontent.com/andrewrapp/arduino-remote-uploader/master/resources/vine-wifi.png" alt="vine" width="300" height="299" border="0" /></a>

Here we are doing the same thing but over wifi with the esp8266 SoC and 3.3V Arduino Pros at 8Mhz.


