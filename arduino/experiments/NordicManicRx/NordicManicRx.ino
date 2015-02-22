#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"

// NOTE: Diecimila/Arduino Pros (w/o CTS) don't auto reset, so need press reset button as soon as this appears in console output:
//         Using Port                    : /dev/tty.usbserial-A4001tRI
//         Using Programmer              : arduino
//         Overriding Baud Rate          : 115200
         
// Set up nRF24L01 radio on SPI bus plus pins 9 & 10
// Leonardo must be on 8,10
RF24 radio(9,10);

// Radio pipe addresses for the 2 nodes to communicate.
//const uint64_t pipes[2] = { 0xF0F0F0F0E1LL, 0xF0F0F0F0D2LL };

uint64_t baseAddress = 0xca05cade05LL;
const uint64_t pipes[2] = { baseAddress, baseAddress + 1 };

uint8_t packet[32];

void dump_buffer(uint8_t arr[], uint8_t len, char context[]) {
    Serial.print(context);
    Serial.print(": ");
  
    for (int i = 0; i < len; i++) {
      Serial.print(arr[i], HEX);
    
      if (i < len -1) {
        Serial.print(",");
      }
    }
  
    Serial.println("");
    Serial.flush();
}

void setup() {

  while (!Serial);
  
  Serial.begin(57600);
  
  delay(4000);
  
  printf_begin();

  Serial.println("Start RX");  

  radio.begin();

  radio.setChannel(0x8);
  
  // experiment
  //radio.setPALevel(RF24_PA_MIN);
  
  radio.setDataRate(RF24_250KBPS);
  
  // optionally, increase the delay between retries & # of retries
  radio.setRetries(3,15);
      
  radio.openWritingPipe(pipes[1]);
  radio.openReadingPipe(1,pipes[0]);
      
  radio.startListening();
  radio.printDetails();
}

uint8_t pipe = 1;

void loop() {
    // check if there is data ready from pipe 1. if we had multiple pipe we'd want to check each for data
    if (radio.available(&pipe)) {
      bool done = false;
      
      // TODO timeout
      while (!done) {
        done = radio.read(packet, 32);
      }
      
      dump_buffer(packet, 32, "Received packet");
      
      // send ack
      radio.stopListening();
      
      // another example suggests a delay for transmitting radio to switch back to listening mode
      delay(20);
      
      //change something
      packet[0] = packet[0] + 0x7f;
      
      bool ok = radio.write(packet, 32);
      
      if (ok) {
        Serial.println("ACK sent");
      } else {
        Serial.println("ACK TX failure");
      }
      
      radio.startListening();
    }
}
