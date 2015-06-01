#include <SoftwareSerial.h>

#define ESP_RX 6
#define ESP_TX 5

SoftwareSerial espSerial(ESP_RX, ESP_TX);

void setup() {
    espSerial.begin(9600);
    Serial.begin(9600); while(!Serial); // UART serial debug
    
    delay(3000);
    
    Serial.println("Up");
}

void loop() {
 while (Serial.available() > 0) {
   espSerial.write(Serial.read());
 } 
 
 while (espSerial.available() > 0) {
   Serial.write(espSerial.read());
 }
}
