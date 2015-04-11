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

#include <SoftwareSerial.h>

#define ESP_RX 3
#define ESP_TX 4
#define ESP_PACKET_SIZE 32

#define REPLY_SIZE 5
#define RESET_PIN 9

SoftwareSerial espSerial(ESP_RX, ESP_TX);

uint8_t replyPayload[] = { MAGIC_BYTE1, MAGIC_BYTE2, 0, 0, 0 };
uint8_t packet[ESP_PACKET_SIZE];

RemoteUploader remoteUploader = RemoteUploader();
extEEPROM eeprom = extEEPROM(kbits_256, 1, 64);

#define readLen 6
// fails without debug enabled, what??
#define DEBUG
#define BUFFER_SIZE 128
#define LISTEN_PORT "1111"

// replace with wifi creds
#define WIFI_NETWORK ""
#define WIFI_PASSWORD ""

#define RESET_MINS 15
//#define SEND_AT_EVERY_MINS 1

long resetEvery = RESET_MINS * 60000;
long lastReset = 0;

char cbuf[BUFFER_SIZE];

// TODO keep track of which channels are open/closed

Stream* debug;
Stream* esp;

void setup() {
  // for Leonardo use &Serial1
  // for atmega328/168 use &Serial
  remoteUploader.setup(&Serial, &eeprom, RESET_PIN);
  //configure debug if an additional Serial port is available. Use Serial with Leonardo
//  remoteUploader.setDebugSerial(&Serial);

  // setup esp
  espSerial.begin(9600); // Soft serial connection to ESP8266
  esp = &espSerial;
  
//  #ifdef DEBUG
//    Serial.begin(9600); while(!Serial); // UART serial debug
//    debug = &Serial;
//  #endif

  //configureEsp8266();
  configureServer();
  
  lastReset = millis();
}


void configureEsp8266() {
/*
<---
AT(13)(13)(10)(13)(10)OK(13)(10)<--[11c],[14ms],[1w]
<---
AT+RST(13)(13)(10)(13)(10)OK(13)(10).|(209)N;(255)P:L(179)(10)BD;G8\(21)E5(205)N^f(5)(161)O'}5"B(201)(168)HHWV.V(175)H(248)<--[58c],[1926ms],[4w]
<---
AT+CWMODE=3(13)(13)(10)(13)(10)OK(13)(10)<--[20c],[25ms],[1w]
<---
AT+CIFSR(13)(13)(10)+CIFSR:APIP,"192.168.4.1"(13)(10)+CIFSR:APMAC,"1a:fe:34:9b:a7:4c"(13)(10)+CIRTP0.0(10)CRSM,8e4ba4(13)(10)(13)<--[96c],[164ms],[1w]
<---
AT+CIPMUX=1(13)(13)(10)(13)(10)OK(13)(10)<--[20c],[25ms],[1w]
<---
AT+CPSRV(164)(245)(197)b(138)(138)(138)(138)(254)(13)(13)(10)(13)(10)OK(13)(10)<--[26c],[18ms],[0w]
<---
AT+CIPMUX=1(13)(13)(10)(13)(10)OK(13)(10)<--[20c],[24ms],[1w]
<---
AT+C(160)M(21)IY(21)I(138)b(138)(138)(254)1(13)(13)(10)no(32)change(13)(10)<--[31c],[21ms],[0w]
*/

  sendAt();
  sendReset();
  sendCwmode();
  //joinNetwork();
  sendCifsr();
  configureServer();
  sendCipmux();
  sendCipServer();
    
  resetCbuf(cbuf, BUFFER_SIZE);
  lastReset = millis();
}

// config to apply after reset or power cycle. everthing else should be retained
void configureServer() {
  sendCipmux();
  sendCipServer();
}

int sendAt() {
  esp->print("AT\r\n");
  
  readFor(100);  
  
  if (strstr(cbuf, "OK") == NULL) {
      return 0;
  }
  
  return 1;
}  

int sendReset() {
  esp->print("AT+RST\r\n");
  
  readFor(2500);  
  
  if (strstr(cbuf, "OK") == NULL) {      
      return 0;
  }
  
  return 1;
}  

int sendCwmode() {
  esp->print("AT+CWMODE=3\r\n");
  
  readFor(100);
  
  if (strstr(cbuf, "OK") == NULL) {
      return 0;
  }
  
  return 1;
}

int joinNetwork() {  
  esp->print("AT+CWJAP=\""); 
  esp->print(WIFI_NETWORK); 
  esp->print("\",\"");
  esp->print(WIFI_PASSWORD);
  esp->print("\"\r\n");
  
  readFor(10000);
  
  if (strstr(cbuf, "OK") == NULL) {
      #ifdef DEBUG
        Serial.println("Join fail");        
      #endif    
      
      return 0;
  }
  
  return 1;
}

int sendCifsr() {
  // on start we could publish our ip address to a known entity
  // or, get from router, or use static ip, 
  esp->print("AT+CIFSR\r\n");
  readFor(200);
  
  // TODO parse ip address
  if (strstr(cbuf, "AT+CIFSR") == NULL) {
      #ifdef DEBUG
        Serial.println("CIFSR fail");        
      #endif    
      
      return 0;
  }
  
  return 1;
}

int sendCipmux() {
  esp->print("AT+CIPMUX=1\r\n"); 
  
  // if still connected: AT+CIPMUX=1(13)(13)(10)link(32)is(32)builded(13)(10)
  readFor(200);    

  if (!(strstr(cbuf, "OK") != NULL || strstr(cbuf, "builded"))) {
      #ifdef DEBUG
        Serial.println("CIPMUX fail");        
      #endif    
      
      return 0;
  }
  
  return 1;
}

int sendCipServer() {
  esp->print("AT+CIPSERVER=1,"); 
  esp->print(LISTEN_PORT);
  esp->print("\r\n");  
  
  readFor(500);  
  
  if (!(strstr(cbuf, "OK") != NULL || strstr(cbuf, "no change"))) {
      #ifdef DEBUG
        Serial.println("CIPSERVER fail");        
      #endif    
      
      return 0;
  }  
  
  return 1;
}

void checkReset() {
  if (millis() - lastReset > resetEvery) {
    #ifdef DEBUG
      Serial.println("Resetting");
    #endif
    
    configureEsp8266();
  } 
}

void resetCbuf(char cbuf[], int size) {
 for (int i = 0; i < size; i++) {
   cbuf[i] = 0; 
 } 
}

int clearSerial() {
  int count = 0;
  while (esp->available() > 0) {
    esp->read();
    count++;
  }
  
  return count;
}

#ifdef DEBUG
void printCbuf(char cbuf[], int len) {
  for (int i = 0; i < len; i++) {
      if (cbuf[i] <= 32 || cbuf[i] >= 127) {
        // not printable. print the char value
        Serial.print("(");
        Serial.print((uint8_t)cbuf[i]);
        Serial.print(")");
      } else {
        Serial.write(cbuf[i]);
      }
  } 
}
#endif

int readChars(char cbuf[], int startAt, int len, int timeout) {  
  int pos = startAt;
  long start = millis();

  while (millis() - start < timeout) {          
    if (esp->available() > 0) {
      uint8_t in = esp->read();
      
      if (in <= 32 || in >= 127 || in == 0) {
        // TODO debug print warning 
      }
      
      cbuf[pos++] = in;
      
      if (pos == len) {
        // null terminate
        cbuf[pos] = 0;
        return len;
      }    
    }
  }
  
  if (millis() - start >= timeout) {
    // timeout
    return -1; 
  }

  return pos;  
}

int readBytes(uint8_t buf[], int startAt, int len, int timeout) {  
  int pos = startAt;
  long start = millis();

  while (millis() - start < timeout) {          
    if (esp->available() > 0) {
      uint8_t in = esp->read();      
      buf[pos++] = in;
      
      if (pos == len) {
        return len;
      }    
    }
  }
  
  if (millis() - start >= timeout) {
    // timeout
    return -1; 
  }

  return pos;  
}

// debugging aid
int readFor(int timeout) {
  long start = millis();
  long lastReadAt = 0;
  bool waiting = false;
  long waits = 0;
  int pos = 0;
  
  #ifdef DEBUG
    Serial.println("<---");
  #endif
  
  resetCbuf(cbuf, BUFFER_SIZE);
  
  while (millis() - start < timeout) {          
    if (esp->available() > 0) {
      if (waiting) {
        waits++;
        waiting = false; 
      }
      
      uint8_t in = esp->read();
      cbuf[pos] = in;
      
      lastReadAt = millis() - start;
      pos++;
      
      if (in <= 32 || in >= 127) {
        // not printable. print the char value
        #ifdef DEBUG
          Serial.print("("); 
          Serial.print(in); 
          Serial.print(")");
        #endif
      } else {
        // pass through
        #ifdef DEBUG
          Serial.write(in);
        #endif
      }
    } else {
      waiting = true;
    }
  }
  
  // null terminate
  cbuf[pos] = 0;
  
  #ifdef DEBUG
    Serial.print("<--[");
    Serial.print(pos);
    Serial.print("c],[");
    Serial.print(lastReadAt);
    Serial.print("ms],[");
    Serial.print(waits);  
    Serial.println("w]");    
  #endif
  
  return pos;
}

int getCharDigitsLength(int number) {
    if (number < 10) {
      return 1;
    } else if (number < 100) {
      return 2;
    } else if (number < 1000) {
      return 3;
    }
  }  

// send data back to client
void sendReply(uint8_t reply[], int size, int channel) {
  // add two for \r\n
  int sendLen = size + 2;
  
  // send AT command with channel and message length
  esp->print("AT+CIPSEND="); 
  esp->print(channel); 
  esp->print(","); 
  esp->print(sendLen); 
  esp->print("\r\n");
  
  // replies with
  //(13)(10)(13)(10)OK(13)(10)AT+CIPSEND=0,4(13)(13)(10)>(32)<--[27]
  // ctrl-c data results in
  //(253)(6)(13)(10)OK(13)(10)AT+CIPSEND=0,4(13)(13)(10)>(32)
  
  // should be same size everytime + send length                    
  int cmdLen = 26 + getCharDigitsLength(sendLen);
  
  int rCmdLen = readChars(cbuf, 0, cmdLen, 5000);
  
  if (rCmdLen == -1) {
    #ifdef DEBUG
      Serial.println("CIPSEND timeout");
    #endif
    
    return;
  } else if (rCmdLen != cmdLen) {
    #ifdef DEBUG
      Serial.print("Error unexp. reply len: "); 
      Serial.println(rCmdLen);
    #endif
    
    return;
  }
  
  if (strstr(cbuf, "busy") != NULL) {
    #ifdef DEBUG
      Serial.print("Busy");
    #endif
    
    return;
  } else if (strstr(cbuf, "CIPSEND") == NULL) {
    #ifdef DEBUG
      Serial.print("Error: ");
      Serial.println(cbuf);
    #endif
    
    return;
  }
  
  #ifdef DEBUG 
    Serial.println("CIPSEND reply");
    printCbuf(cbuf, cmdLen);
  #endif
  
  // send data to client
  esp->write(replyPayload, REPLY_SIZE);
  esp->print("\r\n");
  esp->flush();
  
  // reply
  //ok(13)(13)(10)SEND(32)OK(13)(10)<--[14]

  // fixed length reply
  // we don't really need to read the entire response.. only want to know if it contains "OK"
  int len = readChars(cbuf, 0, REPLY_SIZE + 12, 1000);
  
  if (len == -1) {
    #ifdef DEBUG 
      Serial.println("Data send timeout");            
    #endif
  } else if (len != 12 + REPLY_SIZE) {
    #ifdef DEBUG 
      Serial.println("Reply len err");            
    #endif      
  }

  if (strstr(cbuf, "OK") == NULL) {
    #ifdef DEBUG     
      Serial.print("Error: ");
      Serial.println(cbuf);
    #endif
    return;
  }
}

// send reply to host
// 0 if success, < 0 failure
int sendReply(uint8_t status, uint16_t id, int channel) {
        
  replyPayload[0] = MAGIC_BYTE1;
  replyPayload[1] = MAGIC_BYTE2;
  replyPayload[2] = status;
  replyPayload[3] = (id >> 8) & 0xff;
  replyPayload[4] = id & 0xff;

  sendReply(replyPayload, REPLY_SIZE, channel);
}

void handleProgramming(int channel) {
       if (remoteUploader.isProgrammingPacket(packet, 32)) {
        //dump_buffer(packet, "Received packet", NORDIC_PACKET_SIZE);
        
        int response = remoteUploader.process(packet);
        
        if (response != OK) {
          remoteUploader.reset();
        }
        
//        if (remoteUploader.isFlashPacket(packet)) {
//
//        }
        
        sendReply(response, remoteUploader.getPacketId(packet), channel);          
      } 
}

void handleData() {
  //(13)(10)+IPD,0,4:hi(13)(10)(13)(10)OK(13)(10)
  
  #ifdef DEBUG
    Serial.println("\nGot data");
  #endif

  //ex +IPD,0,10:hi(32)again(13)
  // cbuf ,0,10:    
  
  // debug output
//  readFor(1000);
//  return;
  
  // serial buffer is at comma after D
  char* ipd = cbuf + readLen;
  
  // max channel + length + 2 commas + colon = 9
  int len = esp->readBytesUntil(':', ipd, 9);
  
  //Serial.print("read char to : "); Serial.println(len);
  // space ,0,1:(32)(13)(10)OK(13)(10)(13)(10)
  
  // parse channel
  // null term after channel for atoi
  ipd[2] = 0;
  int channel = atoi(ipd + 1);
  // reset
  ipd[2] = ',';
  
  //ipd[9] = 0;
  // length starts at pos 3
  len = atoi(ipd + 3);
  
  // subtract 2, don't want lf/cr
  len-=2;
  
  if (len <= 0) {
    return; 
  }
  
  if (len > 128) {
    // error.. too large
    #ifdef DEBUG
      Serial.println("Too long");
    #endif
    return;
  }

  // read data into packet
  int rlen = readBytes(packet, 0, len, 2000);

  if (rlen == -1) {
    // timeout
    return;
  } else if (rlen != len) {
    return;
  }

  handleProgramming(channel);
}

void handleConnected() {
  #ifdef DEBUG  
    Serial.println("Connected!");
  #endif
  // discard
  int len = esp->readBytesUntil(10, cbuf, BUFFER_SIZE - 1);
  // TODO parse channel
  // connected[channel] = true;
}

void handleClosed() {
  // TODO handle channel
  //0,CLOSED(13)(10)
  #ifdef DEBUG  
    Serial.println("Conn closed");
  #endif
  // consume line
  int len = esp->readBytesUntil(10, cbuf, BUFFER_SIZE - 1);
}

void loop() {
  //debugLoop();
  
  // the tricky part of AT commands is they vary in length and format, so we don't know how much to read
  // with 6 chars we should be able to identify most allcommands
  if (esp->available() >= readLen) {
    #ifdef DEBUG      
      Serial.print("\n\nSerial available "); 
      Serial.println(esp->available());
    #endif
    
    readChars(cbuf, 0, readLen, 1000);
    
    #ifdef DEBUG
      printCbuf(cbuf, readLen);
    #endif
    
    // not using Serial.find because if it doesn't match we lose the data. so not helpful
    
    if (strstr(cbuf, "+IPD") != NULL) {
      //(13)(10)+IPD,0,4:hi(13)(10)(13)(10)OK(13)(10)      
      handleData();
    } else if (strstr(cbuf, ",CONN") != NULL) {
      //0,CONNECT(13)(10)      
      handleConnected();
    } else if (strstr(cbuf, "CLOS") != NULL) {
      handleClosed();
    } else {
      #ifdef DEBUG
        Serial.println("Unexpected..");
      #endif
      
      readFor(2000);
      
      // assume the worst and reset
      configureEsp8266();
    }
    
    if (false) {
      // health check        
      if (sendAt() != 1) {
        
      }
    }
    
    resetCbuf(cbuf, BUFFER_SIZE);
    // discard remaining. should not be any remaining
    clearSerial();      
  }
  
  checkReset();  
}
