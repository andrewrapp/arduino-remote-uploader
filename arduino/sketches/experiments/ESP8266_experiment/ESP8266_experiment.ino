#include <SoftwareSerial.h>

#define ESP_RX   3
#define ESP_TX   4
SoftwareSerial espSerial(ESP_RX, ESP_TX);

#define readLen 6
#define DEBUG
#define BUFFER_SIZE 128
#define LISTEN_PORT "1111"

// replace with wifi creds
#define WIFI_NETWORK ""
#define WIFI_PASSWORD ""

#define RESET_MINS 15

long resetEvery = RESET_MINS * 60000;
long lastReset = 0;

char cbuf[BUFFER_SIZE];
//uint8_t data[64];
// how many channels are supported?
//bool connected[10];

// TODO keep track of which channels are open/closed

Stream* debug;
Stream* esp;

void setup() {
  // first run AT to see if alive
  delay(3000);
  
  espSerial.begin(9600); // Soft serial connection to ESP8266
  esp = &espSerial;
  
  #ifdef DEBUG
    Serial.begin(9600); while(!Serial); // UART serial debug
    debug = &Serial;
  #endif

  //configureEsp8266();
  configureServer();
  
  lastReset = millis();
}

//int print(char* text) {
//  int len = strlen(text);
//  
//  #ifdef DEBUG
//    debug->print("-->");
//    // ugh
//    esp->print(len);
//    debug->print(text);
//  #endif
//
//  int writeLen = esp->print(text);
//  
//  if (writeLen != len) {
//    #ifdef DEBUG
//      debug->print("Write fail. wrote x, but exp. y");
//    #endif    
//  }
//  
//  return writeLen;
//}

int printDebug(char* text) {
  #ifdef DEBUG
    return debug->print(text);
  #endif
  
  return -1;
}

// from adafruit lib
void debugLoop(void) {
  if(!debug) for(;;); // If no debug connection, nothing to do.

  debug->println("\n========================");
  for(;;) {
    if(debug->available())  esp->write(debug->read());
    if(esp->available()) debug->write(esp->read());
  }
}



void configureEsp8266() {
/*
AT+RST
AT+RST(13)(13)(10)(13)(10)OK(13)(10).|(209)N;(255)P:L(179)(10)BD;(127)6(152)(31)u(21)(204).(233)'(19)F(171)(140)F(29)j(150)(194)(168)HHWV.V(175)H(248)<--[58c],[1925ms],[4w]
Sending AT+CWMODE=3
AT+CWMODE=3(13)(13)(10)(13)(10)OK(13)(10)<--[20c],[24ms],[1w]
Sending AT+CIPMUX=1
// if connected already
AT+CIPMUX=1(13)(13)(10)link(32)is(32)builded(13)(10)
// or
AT+CIPMUX=1(13)(13)(10)(13)(10)OK(13)(10)<--[20c],[25ms],[1w]

AT+CIFSR(13)(13)(10)+CIFSR:APIP,"192.168.4.1"(13)(10)+CIFSR:APMAC,"1a:fe:34:9b:a7:4c"(13)(10)+CRTP0..(13)CSSAC1:::::c(10)O(10)<--[96c],[160ms],[1w]

Sending AT+CIPSERVER=1
AT+IPERV(164)(245)(197)b(138)(154)(154)(178)(13)(13)(10)no(32)change(13)(10)
// but sometimes get
AT+C(160)M(21)IY(21)I(245)(139)b(154)(254)6(13)(13)(10)no(32)change(13)(10)
// OR
AT+C(160)M(21)IY(21)I(245)(139)b(154)36(13)(13)(10)no(32)change(13)(10)
// OR
AT+CPSEVE=1,f(205)(217)j(254)(13)(10)no(32)change(13)(10)
// OR
AT+CPSRVEz(197)(177)(138)(154)(154)(178)(254)(13)(13)(10)(13)(10)OK(13)(10)<--[26c],[22ms],[0w]

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
//  Serial.println("configure done");
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
      #ifdef DEBUG
        printDebug("RST fail");        
      #endif
      
      return 0;
  }
  
  return 1;
}  

int sendCwmode() {
  esp->print("AT+CWMODE=3\r\n");
  
  readFor(100);
  
  if (strstr(cbuf, "OK") == NULL) {
      #ifdef DEBUG
        printDebug("fail");        
      #endif    
      
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
// AT+RST ends with (168)HHWV.V(175)H(248)
int readUntilEndsWith(uint8_t pattern[]) {
  return -1;
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
        Serial.print("("); Serial.print((uint8_t)cbuf[i]); Serial.print(")");
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
          Serial.print("("); Serial.print(in); Serial.print(")");
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
  
  #ifdef DEBUG
    Serial.print("Channel "); Serial.println(channel);
  #endif
  
  //ipd[9] = 0;
  // length starts at pos 3
  len = atoi(ipd + 3);
  
  // subtract 2, don't want lf/cr
  len-=2;
  
  if (len <= 0) {
    #ifdef DEBUG
      Serial.println("no data");
    #endif
    return; 
  }
  
  // reset so we can print
  
  #ifdef DEBUG
    Serial.print("Data len "); Serial.println(len);
  #endif
  
  if (len > 128) {
    // error.. too large
    #ifdef DEBUG
      Serial.println("Too long");
    #endif
    return;
  }

  // read input into data buffer
  int rlen = esp->readBytes(cbuf, len);
  
  if (rlen != len) {
    #ifdef DEBUG
      Serial.print("Data read failure "); Serial.println(rlen);            
    #endif
    return;
  }
 
  // null terminate
  cbuf[len] = 0;
  
  #ifdef DEBUG
    Serial.print("Data:");  
    printCbuf(cbuf, len);
    Serial.println("");  
  #endif
          
  char response[] = "ok";
  
  // NOTE: print or write works for char data, must use print for non-char to print ascii
  
  int sendLen = strlen(response) + 2;
  
  //delay(50);
  
  // send AT command with channel and message length
  esp->print("AT+CIPSEND="); 
  esp->print(channel); 
  esp->print(","); 
  esp->print(sendLen); 
  esp->print("\r\n");
  
  //flush wrecks this device
  //espSerial.flush();
  
  //delay(50);
  
  // replies with
  //(13)(10)(13)(10)OK(13)(10)AT+CIPSEND=0,4(13)(13)(10)>(32)<--[27]
  // ctrl-c data results in
  //(253)(6)(13)(10)OK(13)(10)AT+CIPSEND=0,4(13)(13)(10)>(32)
  
  // should be same size everytime + send length                    
  int cmdLen = 26 + getCharDigitsLength(sendLen);
  
  int rCmdLen = readChars(cbuf, 0, cmdLen, 5000);
  
  if (rCmdLen == -1) {
    #ifdef DEBUG
      Serial.println("AT+CIPSEND timeout");
    #endif
    
    return;
  } else if (rCmdLen != cmdLen) {
    #ifdef DEBUG
      Serial.print("Error unexp. reply len: "); Serial.println(rCmdLen);
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
  
  //delay(100);
  
  // send data to client
  esp->print(response);
  esp->print("\r\n");
  esp->flush();
  
  // reply
  //ok(13)(13)(10)SEND(32)OK(13)(10)<--[14]

  // fixed length reply
  len = readChars(cbuf, 0, strlen(response) + 12, 1000);
  
  if (len == -1) {
    #ifdef DEBUG 
      Serial.println("Data send timeout");            
    #endif
  } else if (len != 12 + strlen(response)) {
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
  
  #ifdef DEBUG     
    Serial.println("Data reply");
    printCbuf(cbuf, len);        
  #endif
  
  //delay(50);
}

void handleConnected() {
  #ifdef DEBUG  
    Serial.println("Connected!");
  #endif
  // discard
  int len = espSerial.readBytesUntil(10, cbuf, 128);
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
  int len = espSerial.readBytesUntil(10, cbuf, 128);
}

void loop() {
  //debugLoop();
  
  // the tricky part of AT commands is they vary in length and format, so we don't know how much to read
  // with 6 chars we should be able to identify most allcommands
  if (espSerial.available() >= readLen) {
    #ifdef DEBUG      
      Serial.print("\n\nSerial available "); Serial.println(espSerial.available());
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
    }
    
    resetCbuf(cbuf, BUFFER_SIZE);
    // discard remaining. should not be any remaining
    clearSerial();      
  }
  
  checkReset();  
}
