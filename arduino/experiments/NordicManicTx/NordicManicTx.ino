#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"

/**
NOTES
 - This uses the original ManiacBug library. https://github.com/maniacbug/RF24 There are many forks; others may work too.
 - I could not get the GettingStarted.pde sketch to work -- would only print send failures. 
 - Seems to work just as well with Arduino power 3.3V as stand-alone 3.3V supply
 - Still seeing lots of erratic failures.. sometimes it will send about 100 before a failure, other times every 3 or so TX
 - Suggestion is to put Use a 10uF cap right before the radio

WIRING

Consult Arduino SPI documentation for pin configuration http://arduino.cc/en/Reference/SPI

Leonardo SPI is only on the ICSP
MOSI - ICSP 4
MISO - ICSP 1
SCK - ICSP 3

ICSP pin 1 is marked on board. pins are as follows with the top being the edge of the board:


---------- <- edge of Leonardo board
2 4 6
* * *
* * *
1 3 5

Diecimila (SPI);
SPI: 10 (SS), 11 (MOSI), 12 (MISO), 13 (SCK)

Nordic wiring:

Bottom (pin side) of Nordic board
----------------------
|            1 * * 2 |
|            3 * * 4 |                           
|            5 * * 6 |                            
|            7 * * 8 |
|                    |
|                    |
----------------------

1 - VCC (3.3V)
2 - GND
3 - CSN 
4 - CE 
5 - MOSI
6 - SCK
7 - IRQ (unconnected)
8 - MISO

Note: radio contructor is radio(CE pin, CSN pin) and library examples use digital 9 for CE and digital 10 for CSN

TODO
 - try different channels
 - ack
 - is there firmware upgrades available?
*/


RF24 radio(9,10);

// Radio pipe addresses for the 2 nodes to communicate.
// 

// choose base address
uint64_t baseAddress = 0xca05cade05LL;
const uint64_t pipes[2] = { baseAddress, baseAddress + 1 };

// 32 bytes
uint8_t data[] = { 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7 };
uint8_t ack[32];

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

void setup(void) {
  while (!Serial);
  Serial.begin(57600);
  
  delay(4000);
  
  printf_begin();
  printf("Start TX\n\r");  

  radio.begin();

  // doesn't say what the default is
  radio.setPALevel(RF24_PA_MAX);
  
  // try a differnt speed RF24_1MBPS
  radio.setDataRate(RF24_250KBPS);
  
  //pick an arbitrary channel. default is 4c
  radio.setChannel(0x8);
  
  //This implementation uses a pre-stablished fixed payload size for all transmissions. If this method is never called, the driver will always transmit the maximum payload size (32 bytes)
  //default is 32 bytes
  //radio.setPayloadSize(4);
  
  radio.setRetries(3,15);
  
  // write to 0, read from 1    
  radio.openWritingPipe(pipes[0]);
  radio.openReadingPipe(1,pipes[1]);      
  radio.startListening();
  radio.printDetails();
}

void loop() {
    // First, stop listening so we can talk.
    radio.stopListening();

    dump_buffer(data, 32, "Sending");
    
    // send 32 bytes
    if (radio.write(data, 32)) {
      Serial.println("ok");
      // wait for ACK
      
      radio.startListening();      
      
      long start = millis();
      
      // wait for ACK
      
      bool timeout = false;
      
      while (!radio.available()) {
        if (millis() - start > 3000) {
          Serial.println("Timeout waiting for ACK");
          timeout = true;
          // timeout
          break;
        }
        
        delay(10);
      }
      
      // Unfortunately once radio.available() returns true, it resets back to false, so I need a timeout variable
      
      if (!timeout) {        
        bool done = false;
          
        while (!done) {
          done = radio.read(ack, 32);
          // TODO dump buffer
          Serial.print("Received ack: ");
          dump_buffer(ack, 32, "ACK");
          Serial.println(ack[0], HEX);
        }        
      }

      radio.stopListening();      
      
      // wait for radio to swtich modes
      delay(20);
    } else {
      Serial.println("TX failure");
      // wait a bit
      delay(500);
    }
}
