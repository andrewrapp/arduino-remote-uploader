# arduino-remote-uploader

This project provides remote upload of Arduino sketches over Wifi (ESP8266), XBee, and Nordic nRF24L01+. For more information see https://medium.com/@nowir3s/arduino-remote-uploader-aa61e4d620b1

<a href="https://vine.co/v/O0jLBw7aO1m" target="_blank"><img src="https://raw.githubusercontent.com/andrewrapp/arduino-remote-uploader/master/resources/vine-xbee.png" alt="vine" width="300" height="299" border="0" /></a>

In the vine, you first see the remote XBee Arduino blinking fast. Then, I update the sketch to blink slower (ever 1 second) and compile it. Next I run the host uploader command to send the sketch to the remote Arduino via XBee and flash the sketch onto the Arduino. The command completes and indicates it the flash was successful. And lastly we see the Arduino is blinking slowly. So there it is: remote Arduino flashing via XBee in six seconds!


