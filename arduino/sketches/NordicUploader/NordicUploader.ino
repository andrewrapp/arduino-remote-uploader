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
#include <extEEPROM.h>
// eeprom requires i2c
#include <Wire.h>
#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"

#include <RemoteUploader.h>

#define PROXY_SERIAL false

#define NORDIC_CE 10
#define NORDIC_CS 11
#define NORDIC_PACKET_SIZE 32
#define RESET_PIN 9

// NOTE: Diecimila/Arduino Pros (w/o CTS) don't auto reset, so need press reset button as soon as this appears in console output:
//         Using Port                    : /dev/tty.usbserial-A4001tRI
//         Using Programmer              : arduino
//         Overriding Baud Rate          : 115200
         
// Set up nRF24L01 radio on SPI bus
RF24 radio(NORDIC_CE, NORDIC_CS);

// 328
//MOSI — Digital 11
//MISO — Digital 12
//SCK — Digital 13
// Leonardo
//MOSI — ICSP 4
//MISO — ICSP 1
//SCK — ICSP 3

uint8_t replyPayload[] = { MAGIC_BYTE1, MAGIC_BYTE2, 0, 0, 0 };

uint64_t baseAddress = 0xca05cade05LL;
const uint64_t pipes[2] = { baseAddress, baseAddress + 1 };

uint8_t packet[NORDIC_PACKET_SIZE];

RemoteUploader remoteUploader = RemoteUploader();
extEEPROM eeprom = extEEPROM(kbits_256, 1, 64);

void setup() {
  // for Leonardo use &Serial1
  // for atmega328/168 use &Serial
  remoteUploader.setup(&Serial, &eeprom, RESET_PIN);
  //configure debug if an additional Serial port is available. Use Serial with Leonardo
  //remoteUploader.setDebugSerial(&Serial);
  
  radio.begin();
  radio.setChannel(0x8);
  radio.setDataRate(RF24_250KBPS);
  // optionally, increase the delay between retries & # of retries
  radio.setRetries(3,15);
  radio.openWritingPipe(pipes[1]);
  radio.openReadingPipe(1,pipes[0]);
  radio.startListening();
}

uint8_t pipe = 1;

// send reply to host
// 0 if success, < 0 failure
int sendReply(uint8_t status, uint16_t id) {
  
  radio.stopListening();
        
  replyPayload[0] = MAGIC_BYTE1;
  replyPayload[1] = MAGIC_BYTE2;
  replyPayload[2] = status;
  replyPayload[3] = (id >> 8) & 0xff;
  replyPayload[4] = id & 0xff;
  
  delay(20);

  bool ok = radio.write(replyPayload, 5);
  
  int success = 0;
  
  if (!ok) {
    //remoteUploader.getDebugSerial()->println("ACK TX fail");
    success = -1;
  }
  
  radio.startListening();
  return success;
}

void loop() {
    // check if there is data ready from pipe 1. if we had multiple pipe we'd want to check each for data
    if (radio.available(&pipe)) {
      bool done = false;
      
      // TODO timeout
      while (!done) {
        done = radio.read(packet, NORDIC_PACKET_SIZE);
      }
      
      if (remoteUploader.isProgrammingPacket(packet, 32)) {
        //dump_buffer(packet, "Received packet", NORDIC_PACKET_SIZE);
        
        int response = remoteUploader.process(packet);
        
        if (response != OK) {
          remoteUploader.reset();
        }
        
        if (remoteUploader.isFlashPacket(packet)) {
          if (PROXY_SERIAL) {
            // revert serial speed if we are proxying
          }
        }
        
        sendReply(response, remoteUploader.getPacketId(packet));          
      }
    }
}
