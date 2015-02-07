void setup() {
  Serial.begin(9600);
  Serial.println("Hello");
}

void loop() {
  while (Serial.available()) {
    //uint8_t b = Serial.read();
    // echo back
    //Serial.write(b);
  } 
//  Serial.println("Hi 8:45");
  delay(1000);
}
