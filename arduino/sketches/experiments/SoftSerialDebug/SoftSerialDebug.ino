#include <SoftwareSerial.h>

const int softTxPin = 11;
const int softRxPin = 12;

SoftwareSerial nss(softTxPin, softRxPin);

void setup() {
  Serial.begin(19200);
  
  // leonardo wait for serial
  while (!Serial)
  
  nss.begin(9600);
}

void loop() {
  while (nss.available() > 0) {
    Serial.print(nss.read(), HEX);
    Serial.print(",");
  }  
}
