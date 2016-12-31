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

#include <RemoteUploader.h>
#include <Esp8266.h>
#include <SoftwareSerial.h>

// NOTE: only some pins support softserial RX on Leonard and Mega boards
#define ESP_RX 8
#define ESP_TX 7

#define ESP_PACKET_SIZE 32

#define REPLY_SIZE 5
#define RESET_PIN 9

#define LED_PIN 13

// TODO send ip address to server on boot for discovery. Otherwise we'd have to guess or query the router's dhcp table (not always possible)

// FIXME client is getting socket timeouts. add ping method to restart if loses ip address
// TODO proxy when not in programming mode

// When true this only tests the ESP communication, does not use the remote uploader library.
// This is useful to debug any issues with ESP using the serial port before throwing flashing into the mix
#define COMMUNICATION_TEST false

SoftwareSerial espSerial(ESP_RX, ESP_TX);
Esp8266 esp8266 = Esp8266();

// this is arbitrary, any port will do
int serverPort = 1111;

uint8_t replyPayload[] = { MAGIC_BYTE1, MAGIC_BYTE2, 0, 0, 0 };
uint8_t packet[ESP_PACKET_SIZE];

RemoteUploader remoteUploader = RemoteUploader();
extEEPROM eeprom = extEEPROM(kbits_256, 1, 64);

/* 6/24 - at times getting ERROR or just an OK on listing networks (with FTDI). Also will not join a network. Could it be the access point rejecting?
AT+CWLAP

ERROR

AT+CWLAP

OK
*/

// 6/25 esp passes communication test on perfboard with DEBUG enabled, but once debug is set to false it gets an IPD error and sketch resets the radio
// setup works fine. Could be the voltage divider can't handle the speed without debug slowdown
// try leonardo with debug
// try 3.3V Arduinos

void setup() {
  
  pinMode(LED_PIN, OUTPUT);   
    
  if (COMMUNICATION_TEST == false) {
    // for Leonardo use &Serial1
    // for atmega328/168 use &Serial
    if (remoteUploader.setup(&Serial, &eeprom, RESET_PIN) != SUCCESS) {
      // eeprom fail
      // optional error reporting
      blink(2, 2000);
    }
    
    //configure debug if an additional Serial port is available (e.g. Leonardo)
    //remoteUploader.setDebugSerial(&Serial);    
    
    // NOTE: Only if you are using a 3.3V/8Mhz I highly recommend running Optiboot at 57.6K instead of 115.2K with 8Mhz Arduinos. This requires the 5V/16Mzh optiboot bootloader
    //remoteUploader.setBaudRate(OPTIBOOT_8MHZ_BAUD_RATE);
  }

  // delay is recommended since if esp8266 is booting it will spew the boot garbage for about 3 seconds. any setup commands would fail
  delay(3000);  
  
  espSerial.begin(9600);
  // configure esp to use softserial
  esp8266.setEspSerial(&espSerial);  
  
  Serial.begin(9600); while(!Serial); // UART serial debug
  
  if (COMMUNICATION_TEST) {
    // make sure esp library has #DEBUG enabled before uncommenting
    //esp8266.setDebugSerial(&Serial);    
  }
  
  // set the packet array for receiving data
  esp8266.setDataByteArray(packet, ESP_PACKET_SIZE);

  // Perform first time esp configuration to connect to AP. Only need to run once
//  if (esp8266.configure("ssid","password") != SUCCESS) {
//    if (COMMUNICATION_TEST) {
//      Serial.println("Configure failed");
//    }
//    
//    blink(5, 1000);
//    for (;;) {}
//  }

  while (!esp8266.configureServer(serverPort)) {      
    blink(4, 1000);       
    
    if (COMMUNICATION_TEST) {
      Serial.println("Failed to configure server");
    }
  }
  
  // indicate successful setup
  blink(1, 50);  
}

void blink(int times, int wait) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(wait);
    digitalWrite(LED_PIN, LOW);
    
    if (i < times - 1) {
      delay(wait);      
    }
  }
}

// send reply to host
int sendReply(uint8_t status, uint16_t id) {
  replyPayload[0] = MAGIC_BYTE1;
  replyPayload[1] = MAGIC_BYTE2;
  replyPayload[2] = status;
  replyPayload[3] = (id >> 8) & 0xff;
  replyPayload[4] = id & 0xff;
    
  // send reply
  if (esp8266.send(esp8266.getChannel(), replyPayload, REPLY_SIZE) != SUCCESS) {
    if (COMMUNICATION_TEST) {
      Serial.println("Failed to send reply");
    }
  }
}

void handleProgramming() {
  // send the packet array, length to be processed
  int response;
  
  if (COMMUNICATION_TEST) {
    // send back a success
    response = OK;
  } else {
    response = remoteUploader.process(packet);                    
  
    // TODO do reset in library
    if (response != OK) {
      remoteUploader.reset();
    }          
  }
  
  sendReply(response, remoteUploader.getPacketId(packet));          
    
  if (remoteUploader.isFlashPacket(packet)) {
    // flash is complete
  }
}

void loop() {
  if (esp8266.readSerial()) { 
    if (esp8266.isData()) {
      // got data
      if (esp8266.getDataLength() > 4 && remoteUploader.isProgrammingPacket(packet, esp8266.getDataLength())) {
        handleProgramming();
      } else {
        // TODO not programming, forward
      }
    } else if (esp8266.isConnect()) {
        if (remoteUploader.inProgrammingMode() == false) {
          // TODO forward
        }
    } else if (esp8266.isDisconnect()) {
        if (remoteUploader.inProgrammingMode() == false) {
          // TODO forward
        }
    } 
  } else if (esp8266.isError()) {
    blink(3, 50);
    
    if (COMMUNICATION_TEST) {
      Serial.print("Failed on command ");
      Serial.print(esp8266.getLastCommand());
      Serial.print(" with error ");
      Serial.println(esp8266.getLastResult());          
    } else {
      // attempt to send error to client. may not work 
    } 
    
    if (esp8266.getLastCommand() == UNKNOWN_COMMAND) {
      if (COMMUNICATION_TEST) {
        Serial.print("Resetting");
      }
      
      // restart
      // assume the worst and reset
      esp8266.restartEsp8266();
      // need to apply server config which is lost on restart
      esp8266.configureServer(serverPort);
    }
  } else {
   // no data 
  }
}
