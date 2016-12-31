int led = 13;

// Minimal sketch for testing remote firmware flashing

// Update yuur Ardiuno IDE preferences and select show verbose output during compilation and upload

// NOTE select board type for the application arduino, not the programmer!
// Click verify and get the path to the hex
// e.g. /var/folders/g1/vflh_srj3gb8zvpx_r5b9phw0000gn/T/build4977709782306425433.tmp/Blink.cpp.hex

void setup() {                
  pinMode(led, OUTPUT);     
}

int wait = 50;
// int wait = 5000;

void loop() {
  digitalWrite(led, HIGH);
  delay(wait);
  digitalWrite(led, LOW);
  delay(wait);
}
