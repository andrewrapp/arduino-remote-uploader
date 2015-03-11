int led = 13;

// NOTE select board type for the application arduino, not the programmer!
// Click verify and get the path to the hex
// e.g. /var/folders/g1/vflh_srj3gb8zvpx_r5b9phw0000gn/T/build4977709782306425433.tmp/Blink.cpp.hex

void setup() {                
  pinMode(led, OUTPUT);     
}

int wait = 50;

void loop() {
  digitalWrite(led, HIGH);
  delay(wait);
  digitalWrite(led, LOW);
  delay(wait);
}
