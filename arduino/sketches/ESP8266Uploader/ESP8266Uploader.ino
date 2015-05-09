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

#define ESP_RX 3
#define ESP_TX 4
#define ESP_PACKET_SIZE 32

#define REPLY_SIZE 5
#define RESET_PIN 9

// Tests the ESP communication
// this is useful to debug any issues with ESP with the serial port before throwing flashing into the mix
#define PING_TEST true

SoftwareSerial espSerial(ESP_RX, ESP_TX);
Esp8266 esp8266 = Esp8266();

// this is arbitrary, any port will do
int serverPort = 1111;

uint8_t replyPayload[] = { MAGIC_BYTE1, MAGIC_BYTE2, 0, 0, 0 };
uint8_t packet[ESP_PACKET_SIZE];

RemoteUploader remoteUploader = RemoteUploader();
extEEPROM eeprom = extEEPROM(kbits_256, 1, 64);

void setup() {
  if (PING_TEST == false) {
    // for Leonardo use &Serial1
    // for atmega328/168 use &Serial
    remoteUploader.setup(&Serial, &eeprom, RESET_PIN);    
    //configure debug if an additional Serial port is available. Use Serial with Leonardo
    //remoteUploader.setDebugSerial(&Serial);    
    
    // NOTE if you are using a 3.3V/8Mhz, it will probably not work at 115.2K baud so load the 5V/16Mzh optiboot bootloader so it runs at 56.7K
    remoteUploader.setBaudRate(OPTIBOOT_8MHZ_BAUD_RATE);
  }

  // delay is recommended since if esp8266 is booting it will spew the boot garbage for about 3 seconds. any setup commands would fail
  delay(3000);  
  espSerial.begin(9600);
  // configure esp to use softserial
  esp8266.setEspSerial(&espSerial);  
  Serial.begin(9600); while(!Serial); // UART serial debug
  
  // we can only use the Serial for debug when testing esp
  if (PING_TEST) {
    esp8266.setDebugSerial(&Serial);    
  }
  
  // make sure this is big enough to read your packet or you'll get unexpected results
  esp8266.setDataByteArray(packet, 32);

  // only the first time do you need to connect to the AP. this is retained by esp8266
//  if (esp8266.configure("ssid","password") != SUCCESS) {
//    if (PING_TEST) {
//      Serial.println("Configure failed");
//    }
//    
//    for (;;) {}
//  }

  while (!esp8266.configureServer(serverPort)) {
      if (PING_TEST) {
        Serial.println("Failed to configure server");
      }
      delay(3000);
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
    if (PING_TEST) {
      Serial.println("Failed to send reply");
    }
  }
}

void handleProgramming() {
  // send the packet array, length to be processed
  int response;
  
  if (PING_TEST) {
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
    if (PING_TEST) {
      Serial.print("Failed on command ");
      Serial.print(esp8266.getLastCommand());
      Serial.print(" with error ");
      Serial.println(esp8266.getLastResult());          
    } else {
      // attempt to send error to client. may not work 
    } 
    
    if (esp8266.getLastCommand() == UNKNOWN_COMMAND) {
      if (PING_TEST) {
        Serial.print("Resetting");
      }
      
      // reset
      // assume the worst and reset
      esp8266.restartEsp8266();
      // need to apply server config which is lost on restart
      esp8266.configureServer(serverPort);
    }
  } else {
   // no data 
  }
}
