#include <SoftwareSerial.h>

#define ESP_RX   3
#define ESP_TX   4
SoftwareSerial espSerial(ESP_RX, ESP_TX);

#define readLen 6
#define DEBUG
#define BUFFER_SIZE 128

char cbuf[BUFFER_SIZE];
//uint8_t data[64];
// how many channels are supported?
bool connected[10];

// TODO keep track of which channels are open/closed

void setup() {
  espSerial.begin(9600); // Soft serial connection to ESP8266
  Serial.begin(9600); while(!Serial); // UART serial debug
  
  #ifdef DEBUG
    Serial.println("Waiting 3s");
    delay(3000);  
  #endif

  // TODO periodic reset and setup
  
  
/*
Sending AT+CWMODE=3
AT+CWMODE=3(13)(13)(10)(13)(10)OK(13)(10)
Sending AT+CIPMUX=1
// if connected already
AT+CIPMUX=1(13)(13)(10)link(32)is(32)builded(13)(10)
Sending AT+CIPSERVER=1
AT+IPERV(164)(245)(197)b(138)(154)(154)(178)(13)(13)(10)no(32)change(13)(10)
// but sometimes get
AT+C(160)M(21)IY(21)I(245)(139)b(154)(254)6(13)(13)(10)no(32)change(13)(10)
// OR
AT+C(160)M(21)IY(21)I(245)(139)b(154)36(13)(13)(10)no(32)change(13)(10)
// OR
AT+CPSEVE=1,f(205)(217)j(254)(13)(10)no(32)change(13)(10)
*/

//  Serial.println("Sending AT+RST");
//  espSerial.print("AT+RST\r\n");
//  readFor(3000);  

  #ifdef DEBUG
    Serial.println("Sending AT+CWMODE=3");
  #endif
  
  espSerial.print("AT+CWMODE=3\r\n");
  readFor(100);

//  Serial.println("Sending AT+CIFSR");
//  espSerial.print("AT+CIFSR\r\n");
//  readFor(5000);

  #ifdef DEBUG
    Serial.println("\nSending AT+CIPMUX=1");  
  #endif    
  espSerial.print("AT+CIPMUX=1\r\n");
  // if still connected: AT+CIPMUX=1(13)(13)(10)link(32)is(32)builded(13)(10)
  readFor(100);  
  // inconsistent but sometimes returns no(32)change(13)

  #ifdef DEBUG
  Serial.println("\nSending AT+CIPSERVER=1");    
  #endif
  
  espSerial.print("AT+CIPSERVER=1,1336\r\n");
  readFor(100);
  
  resetCbuf(cbuf, BUFFER_SIZE);
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
  while (espSerial.available() > 0) {
    espSerial.read();
    count++;
  }
  
  return count;
}

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

int readChars(char cbuf[], int startAt, int len, int timeout) {  
  int pos = startAt;
  long start = millis();

  while (millis() - start < timeout) {          
    if (espSerial.available() > 0) {
      uint8_t in = espSerial.read();
      
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
    Serial.println("-->");
  #endif
  
  while (millis() - start < timeout) {          
    if (espSerial.available() > 0) {
      if (waiting) {
        waits++;
        waiting = false; 
      }
      
      uint8_t in = espSerial.read();
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
    Serial.println("\nGot IPD");
  #endif
  
  //ex +IPD,0,10:hi(32)again(13)
  // cbuf ,0,10:    
  
  // serial buffer is at comma after D
  char* ipd = cbuf + readLen;
  
  // max channel + length + 2 commas + colon = 9
  int len = espSerial.readBytesUntil(':', ipd, 9);
  
  // parse channel
  // null term after channel for atoi
  ipd[2] = 0;
  int channel = atoi(ipd + 1);
  // reset
  ipd[2] = ',';
  
  #ifdef DEBUG
    Serial.print("Channel "); Serial.println(channel);
  #endif
  
  // length starts at pos 3
  len = atoi(ipd + 3);
  
  // subtract 2, don't want lf/cr
  len-=2;

  #ifdef DEBUG
    Serial.print("IPD: "); Serial.println(cbuf);
  #endif
  
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
  int rlen = espSerial.readBytes(cbuf, len);
  
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
    Serial.println("Echoing");    
  #endif
          
  char response[] = "ok";
  
  // NOTE: print or write works for char data, must use print for non-char to print ascii
  
  int sendLen = strlen(response) + 2;
  
  // help with busy reply???
  delay(500);
  
  // send AT command with channel and message length
  espSerial.print("AT+CIPSEND="); espSerial.print(channel); espSerial.print(","); espSerial.print(sendLen); espSerial.print("\r\n");
  //espSerial.flush();
  
  // replies with
  //(13)(10)(13)(10)OK(13)(10)AT+CIPSEND=0,4(13)(13)(10)>(32)<--[27]
  // ctrl-c data results in
  //(253)(6)(13)(10)OK(13)(10)AT+CIPSEND=0,4(13)(13)(10)>(32)
  
  // should be same size everytime + send length                    
  int cmdLen = 26 + getCharDigitsLength(sendLen);
  
  int rCmdLen = readChars(cbuf, 0, cmdLen, 5000);
  
  if (rCmdLen == -1) {
    
    Serial.println("AT+CIPSEND timeout");
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
    Serial.println("");
    // TODO check for AT+CIPSEND or not ERROR
    Serial.println("Sending");    
  #endif
          
  // send data to client
  espSerial.print(response);
  espSerial.print("\r\n");
  //espSerial.flush();
  
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
  // the tricky part of AT commands is they vary in length and format, so we don't know how much to read
  // with 6 chars we should be able to identify most allcommands
  if (espSerial.available() >= readLen) {
    //Serial.setTimeout()

    #ifdef DEBUG      
      Serial.println("Read serial");
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
      // TODO get rest of string and print
      // unexpected print 
      #ifdef DEBUG
        Serial.println("Unexpected..");
      #endif
      readFor(100);
    }

    // discard remaining. should not be any remaining
    clearSerial();      
    //resetCbuf(cbuf, 129);
  }
}
