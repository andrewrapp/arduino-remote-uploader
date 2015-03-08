#include "eeprom_flasher.h"

#include <extEEPROM.h>
// eeprom requires i2c
#include <Wire.h>

#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"

#define NORDIC_CE 10
#define NORDIC_CS 11

// NOTE: can't use printDetails with Arduino Pro, of course the serial port is needed by the programmer

// NOTE: Diecimila/Arduino Pros (w/o CTS) don't auto reset, so need press reset button as soon as this appears in console output:
//         Using Port                    : /dev/tty.usbserial-A4001tRI
//         Using Programmer              : arduino
//         Overriding Baud Rate          : 115200
         
// Set up nRF24L01 radio on SPI bus
RF24 radio(NORDIC_CE, NORDIC_CS);

uint8_t replyPayload[] = { MAGIC_BYTE1, MAGIC_BYTE2, 0 };

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
  int setup_success = setup_core();
  
  // TODO handle setup_success != OK
//  Serial.begin(57600);
  
  delay(4000);
  // can only use printf with Leonardo
  //printf_begin();
  //Serial.println("Start RX");  

  radio.begin();

  radio.setChannel(0x8);
  radio.setDataRate(RF24_250KBPS);
  // optionally, increase the delay between retries & # of retries
  radio.setRetries(3,15);
  radio.openWritingPipe(pipes[1]);
  radio.openReadingPipe(1,pipes[0]);
  radio.startListening();
  //radio.printDetails();
}

uint8_t pipe = 1;

int sendReply(uint8_t status) {
  
  radio.stopListening();
        
  replyPayload[0] = MAGIC_BYTE1;
  replyPayload[1] = MAGIC_BYTE2;
  replyPayload[2] = status;
  
  delay(20);

  // can we do this. does it need to be 32 bytes?
  bool ok = radio.write(replyPayload, 3);
  
  int success = 0;
  
  if (!ok) {
    #if (USBDEBUG || NSSDEBUG) 
      getDebugSerial()->println("ACK TX fail");
    #endif 
    success = -1;
  }
  
  radio.startListening();
  return success;
}

//uint8_t getPacketLength(packet[]) {
//  
//}

void loop() {
    // check if there is data ready from pipe 1. if we had multiple pipe we'd want to check each for data
    if (radio.available(&pipe)) {
      bool done = false;
      
      // TODO timeout
      while (!done) {
        done = radio.read(packet, 32);
      }
      
      if (isProgrammingPacket(packet, 32)) {
        // send the packet array, length to be processed
        int response = handlePacket(packet, 32);
        
        if (response != OK) {
          prog_reset();
        }
        
        if (isFlashPacket(packet)) {
          if (PROXY_SERIAL) {
            // not implemented
          }
        }
        
        sendReply(response);          
      }
          
      dump_buffer(packet, 32, "Received packet");
    }
}
