/**
 * Copyright (c) 2015 Andrew Rapp. All rights reserved.
 *
 * This file is part of arduino-sketcher
 *
 * arduino-sketcher is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * arduino-sketcher is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with arduino-sketcher.  If not, see <http://www.gnu.org/licenses/>.
 */
 
 /**
 * Macs and most PCs don't have SPI so I'm using an Arduino with this sketch to communicate with the Nordic radio.
 * On a raspberrypi it should be possible to communicate directly with a nordic as it has SPI
 */
 
#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"

#define MAGIC_BYTE1 0xef
#define MAGIC_BYTE2 0xac

#define NORDIC_CE 10
#define NORDIC_CS 11
#define NORDIC_PACKET_SIZE 32

#define TX_LOOP_RETRIES 10

#define SUCCESS 0
#define TX_FAILURE 1
#define ACK_FAILURE 2
#define START_OVER 3
#define TIMEOUT 4

#define ACK_TIMEOUT 3000
#define SERIAL_TIMEOUT 5000

RF24 radio(NORDIC_CE, NORDIC_CS);

// Radio pipe addresses for the 2 nodes to communicate.
// choose base address
uint64_t baseAddress = 0xca05cade05LL;
const uint64_t pipes[2] = { baseAddress, baseAddress + 1 };

// 32 bytes
uint8_t data[NORDIC_PACKET_SIZE];
uint8_t ack[NORDIC_PACKET_SIZE];
uint8_t b;
uint8_t last_byte;

long last_packet;

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
  Serial.begin(19200);

  radio.begin();

  // doesn't say what the default is
  radio.setPALevel(RF24_PA_MAX);
  
  // try a differnt speed RF24_1MBPS
  radio.setDataRate(RF24_250KBPS);
  
  //pick an arbitrary channel. default is 4c
  radio.setChannel(0x8);
  radio.setRetries(3,15);
  
  // write to 0, read from 1    
  radio.openWritingPipe(pipes[0]);
  radio.openReadingPipe(1,pipes[1]);      
  radio.startListening();
}

uint8_t data_pos = 0;
uint8_t length = 0;

bool parsing = false;

int send_packet() {
  int success = SUCCESS;
  
  radio.stopListening();

  //dump_buffer(data, 32, "Sending packet to nordic");        
        
  if (!radio.write(data, 32)) {
    success = TX_FAILURE;         
  } else {
    // wait for ACK
    radio.startListening();      
      
    long start = millis();
      
    // wait for ACK
    bool timeout = false;

    // Unfortunately once radio.available() returns true, it resets back to false, so I need a timeout variable      
    while (!radio.available()) {
      if (millis() - start > ACK_TIMEOUT) {
        timeout = true;
        return ACK_FAILURE;
      }
    }
      
    while (!radio.read(ack, 32)) {
      
    } 
    
    // TODO import codes from header
    if (ack[2] == 1) {
      // ok!
    } else if (ack[2] == 2) {
      success = START_OVER;
    } else if (ack[2] == 3) {     
      success = TIMEOUT;
    }
        
    //dump_buffer(ack, 32, "ACK");
    //Serial.println(ack[2], HEX);    
      
    // wait for radio to swtich modes
    delay(20); 
              
    radio.startListening();   
      
    return success;
  }
}

void reset() {
  data_pos = 0;
  parsing = false;
  last_byte = -1;
  
  for (int i = 0; i < 32; i++) {
    data[i] = 0;
  }
}

void loop() {
  while (Serial.available() > 0) {
    b = Serial.read();

    if (!parsing) {
      // look for programming packet

      if (last_byte == MAGIC_BYTE1 && b == MAGIC_BYTE2) {
          //Serial.println("Packet start");
          parsing = true;        
          data[0] = MAGIC_BYTE1;
          data_pos = 1;
      }      
    }
    
    last_byte = b;
    
    if (parsing) {
      //Serial.print("assigning "); Serial.print(b, HEX); Serial.print(" at pos "); Serial.println(data_pos);
      
      data[data_pos] = b;
      last_packet = millis();
     
      if (data_pos == 3) {
//        Serial.print("length is "); Serial.println(b, HEX);
        length = b;
      }
     
      if (length > 0 && data_pos + 1 == length) {
        // complete packet
        
        bool sent = false;
        
        // TODO instead of retries send back a resend
        for (int i = 0; i < TX_LOOP_RETRIES; i++) {
          int response = send_packet();

          if (response == SUCCESS) {
            sent = true;
            break; 
          } else if (response == TX_FAILURE || response == ACK_FAILURE) {
            if (response == TX_FAILURE) {
              Serial.println("TX failure");
            } else {
              Serial.println("No ack failure");
            }
            
            Serial.println("Retrying...");
            delay(100);
          } else if (response == START_OVER) {
            Serial.println("Start over");
            break;
          } else if (response == TIMEOUT) {
            Serial.println("Timeout");
            break;
          } else {
            // what is this?? <-Unexpected response -24320
            Serial.print("Unexpected response: "); Serial.println(response, DEC);
            break; 
          }
        }  
        
        if (sent) {
          Serial.println("OK");
        } else {
          // failed with retries
          Serial.println("ERROR: Unable to transmit");        
        }
        
        reset();
      } else {
        data_pos++;       
      }
    }
  }

  if (parsing && millis() - last_packet > SERIAL_TIMEOUT) {
    Serial.println("ERROR: Timeout");
    reset();
  }  
}
