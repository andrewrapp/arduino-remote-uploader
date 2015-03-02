int led = 13;

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
