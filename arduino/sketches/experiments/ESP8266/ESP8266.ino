/**
DEPRECATED -- this code and many bug fixes have been migrated to the ESP library
*/

#include <SoftwareSerial.h>

#define DEBUG

#define UNKNOWN_COMMAND 0
#define IPD_COMMAND 1
#define CONNECTED_COMMAND 2
#define DISCONNECTED_COMMAND 3

#define ESP_RX 3
#define ESP_TX 4
// how many chars to read to identify command
#define COMMAND_LEN 6
// fails without debug enabled, what??
#define BUFFER_SIZE 128
#define LISTEN_PORT "1111"
// replace with wifi creds
#define WIFI_NETWORK ""
#define WIFI_PASSWORD ""
#define CONNECT_CMD_LEN 11
#define CLOSED_CMD_LEN 10

// may not be necessary. instead do a AT command and only reset if no response
#define RESET_MINS 180
//#define SEND_AT_EVERY_MINS 1

SoftwareSerial espSerial(ESP_RX, ESP_TX);

long resetEvery = RESET_MINS * 60000;
long lastReset = 0;

char cbuf[BUFFER_SIZE];

// TODO keep track of which channels are open/closed

Stream* debug;
Stream* esp;

// documentation seems to indciate a max of 5 connections but not very clear
bool connections[10];

Stream* getEspSerial() {
  return esp;
}

Stream* getDebugSerial() {
  return debug;
}

void setup() {  
  espSerial.begin(9600); // Soft serial connection to ESP8266
  esp = &espSerial;
  
  // TODO check for reset on startup. if both devices are powered on then ESP will send reset gibberish
  
  #ifdef DEBUG
    Serial.begin(9600); while(!Serial); // UART serial debug
    debug = &Serial;
  #else
    debug = NULL;
  #endif

  //configureEsp8266();
  configureServer();
  
  lastReset = millis();

  #ifdef DEBUG
    getDebugSerial()->println("ok setup");
  #endif
}

void resetEsp8266() {
  for (int i = 0; i < 10; i++) {
    if (connections[i]) {
      closeConnection(i);  
    }
  }
  
  stopServer();
  sendRestart();
  configureServer();
    
  resetCbuf(cbuf, BUFFER_SIZE);
  lastReset = millis();  
}

int printDebug(char* text) {
  #ifdef DEBUG
    return getDebugSerial()->print(text);
  #endif
  
  return -1;
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
  sendRestart();
  sendCwmode();
  //joinNetwork();
  sendCifsr();
  configureServer();
  enableMultiConnections();
  startServer();
    
  resetCbuf(cbuf, BUFFER_SIZE);
  lastReset = millis();
}

// config to apply after reset or power cycle. everthing else should be retained
void configureServer() {
  enableMultiConnections();
  startServer();
}

// TODO static ip: AT+CIPSTA

int sendAt() {
  getEspSerial()->print("AT\r\n");
  
  readFor(100);  
  
  if (strstr(cbuf, "OK") == NULL) {
      return 0;
  }
  
  return 1;
}  

int sendRestart() {
  getEspSerial()->print("AT+RST\r\n");
  
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
  getEspSerial()->print("AT+CWMODE=3\r\n");
  
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
  getEspSerial()->print("AT+CWJAP=\""); 
  getEspSerial()->print(WIFI_NETWORK); 
  getEspSerial()->print("\",\"");
  getEspSerial()->print(WIFI_PASSWORD);
  getEspSerial()->print("\"\r\n");
  
  readFor(10000);
  
  if (strstr(cbuf, "OK") == NULL) {
      #ifdef DEBUG
        getDebugSerial()->println("Join fail");        
      #endif    
      
      return 0;
  }
  
  return 1;
}

int sendCifsr() {
  // on start we could publish our ip address to a known entity
  // or, get from router, or use static ip, 
  getEspSerial()->print("AT+CIFSR\r\n");
  readFor(200);
  
  // TODO parse ip address
  if (strstr(cbuf, "AT+CIFSR") == NULL) {
      #ifdef DEBUG
        getDebugSerial()->println("CIFSR fail");        
      #endif    
      
      return 0;
  }
  
  return 1;
}

// required for server mode
// set to 0 for single connection
int enableMultiConnections() {
  getEspSerial()->print("AT+CIPMUX=1\r\n"); 
  
  // if still connected: AT+CIPMUX=1(13)(13)(10)link(32)is(32)builded(13)(10)
  readFor(200);    

  if (!(strstr(cbuf, "OK") != NULL || strstr(cbuf, "builded"))) {
      #ifdef DEBUG
        getDebugSerial()->println("CIPMUX fail");        
      #endif    
      
      return 0;
  }
  
  return 1;
}

int closeConnection(int id) {
  getEspSerial()->print("AT+CIPCLOSE=");
  getEspSerial()->print(id);
  getEspSerial()->print("\r\n"); 
  
  readFor(200);    

  if (strstr(cbuf, "OK") == NULL) {
      #ifdef DEBUG
        getDebugSerial()->println("Close conn fail");        
      #endif    
      
      return 0;
  }
  
  return 1;  
}

// must enable multi connections prior to calling
int startServer() {
  startStopServer(true);  
}

// stop server and close connections. must call reset after
int stopServer() {
  startStopServer(false);
}

int startStopServer(bool start) {
  getEspSerial()->print("AT+CIPSERVER="); 
  if (start) {
    getEspSerial()->print(1);
  } else {
    getEspSerial()->print(0);
  }
  
  if (start) {
    getEspSerial()->print(",");
    getEspSerial()->print(LISTEN_PORT);    
  }

  getEspSerial()->print("\r\n");  
  
  readFor(500);  
  
  if (!(strstr(cbuf, "OK") != NULL || strstr(cbuf, "no change") || strstr(cbuf, "restart"))) {
      #ifdef DEBUG
        getDebugSerial()->println("CIPSERVER fail");        
      #endif    
      
      return 0;
  }  
  
  return 1;
}

void checkReset() {
  if (millis() - lastReset > resetEvery) {
    #ifdef DEBUG
      getDebugSerial()->println("Resetting");
    #endif
    
    resetEsp8266();
//    configureEsp8266();
  } 
}

void resetCbuf(char cbuf[], int size) {
 for (int i = 0; i < size; i++) {
   cbuf[i] = 0; 
 } 
}

void printCbuf(char cbuf[], int len) {
  #ifdef DEBUG  
    long start = millis();
  
    for (int i = 0; i < len; i++) {
        if (cbuf[i] <= 32 || cbuf[i] >= 127) {
          // not printable. print the char value
          getDebugSerial()->print("(");
          getDebugSerial()->print((uint8_t)cbuf[i]);
          getDebugSerial()->print(")");
        } else {
          getDebugSerial()->write(cbuf[i]);
        }
    }
   
    long end = millis();
    getDebugSerial()->print(" in ");
    getDebugSerial()->print(end - start);
    getDebugSerial()->println("ms");
  #endif
}

int readChars(char cbuf[], int startAt, int len, int timeout) {  
  int pos = startAt;
  long start = millis();

  while (millis() - start < timeout) {          
    if (getEspSerial()->available() > 0) {
      uint8_t in = getEspSerial()->read();
      
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

int readBytesUntil(char cbuf[], uint8_t match, int maxReadLen, int timeout) {  
  int pos = 0;
  long start = millis();

  while (millis() - start < timeout) {          
    if (getEspSerial()->available() > 0) {
      uint8_t in = getEspSerial()->read();
      
      // don't include the match byte
      if (match == in) {        
        return pos;  
      }
      
      cbuf[pos++] = in;
      
      if (pos == maxReadLen) {
        return 0;
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
    getDebugSerial()->println("<---");
  #endif
  
  resetCbuf(cbuf, BUFFER_SIZE);
  
  while (millis() - start < timeout) {          
    if (getEspSerial()->available() > 0) {
      if (waiting) {
        waits++;
        waiting = false; 
      }
      
      uint8_t in = getEspSerial()->read();
      cbuf[pos] = in;
      
      lastReadAt = millis() - start;
      pos++;
      
      if (in <= 32 || in >= 127) {
        // not printable. print the char value
        #ifdef DEBUG
          getDebugSerial()->print("("); 
          getDebugSerial()->print(in); 
          getDebugSerial()->print(")");
        #else
          delay(2);
        #endif
      } else {
        // pass through
        #ifdef DEBUG
          getDebugSerial()->write(in);
        #endif
      }
    } else {
      waiting = true;
    }
  }
  
  // null terminate
  cbuf[pos] = 0;
  
  #ifdef DEBUG
    getDebugSerial()->print("<--[");
    getDebugSerial()->print(pos);
    getDebugSerial()->print("c],[");
    getDebugSerial()->print(lastReadAt);
    getDebugSerial()->print("ms],[");
    getDebugSerial()->print(waits);  
    getDebugSerial()->println("w]");    
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

int sendReply(int channel, char response[]) {
  
  if (!connections[channel]) {
    // not connected on this id
    return -99;
  }
  // NOTE: print or write works for char data, must use print for non-char to print ascii
  
  int sendLen = strlen(response) + 2;
  
  // send AT command with channel and message length
  // TODO get write len
  getEspSerial()->print("AT+CIPSEND="); 
  getEspSerial()->print(channel); 
  getEspSerial()->print(","); 
  getEspSerial()->print(sendLen); 
  getEspSerial()->print("\r\n");
  // don't use flush!
  //getEspSerial()->flush();
  
  // replies with
  //(13)(10)(13)(10)OK(13)(10)AT+CIPSEND=0,4(13)(13)(10)>(32)<--[27]
  
  // should be same size everytime + send length                    
  int cmdLen = 26 + getCharDigitsLength(sendLen);
  
  int rCmdLen = readChars(cbuf, 0, cmdLen, 5000);
  
  if (rCmdLen == -1) {
    #ifdef DEBUG
      getDebugSerial()->println("CIPSEND timeout");
    #endif
    
    return -5;
  } else if (rCmdLen != cmdLen) {
    #ifdef DEBUG
      getDebugSerial()->print("Error unexp. reply len: "); 
      getDebugSerial()->println(rCmdLen);
    #endif
    
    return -6;
  }
  
  if (strstr(cbuf, "busy") != NULL) {
    #ifdef DEBUG
      getDebugSerial()->print("Busy");
    #endif
    
    return -7;
  } else if (strstr(cbuf, "OK") == NULL) {
    #ifdef DEBUG
      getDebugSerial()->print("Error: ");
      getDebugSerial()->println(cbuf);
    #endif
    
    return -8;
  }
  
  #ifdef DEBUG 
    getDebugSerial()->println("CIPSEND reply");
    printCbuf(cbuf, cmdLen);
  #endif
  
  // send data to client
  getEspSerial()->print(response);
  getEspSerial()->print("\r\n");
  getEspSerial()->flush();
  
  // reply
  //ok(13)(13)(10)SEND(32)OK(13)(10)<--[14]

  // fixed length reply
  // timed out a few times over 12h period with 1s timeout. increasing timeout to 5s
  int len = readChars(cbuf, 0, strlen(response) + 12, 5000);
  
  if (len == -1) {
    #ifdef DEBUG 
      getDebugSerial()->println("Data send timeout");            
    #endif
    return -11;
  } else if (len != 12 + strlen(response)) {
    #ifdef DEBUG 
      getDebugSerial()->println("Reply len err");            
    #endif     
    return -12; 
  }

  if (strstr(cbuf, "OK") == NULL) {
    #ifdef DEBUG     
      getDebugSerial()->print("Error: ");
      getDebugSerial()->println(cbuf);
    #endif
    return -9;
  }
  
  #ifdef DEBUG     
    getDebugSerial()->println("Data reply");
    printCbuf(cbuf, len);        
  #endif  
  
  return 1;
}

int handleData() {
  //(13)(10)+IPD,0,4:hi(13)(10)(13)(10)OK(13)(10)
  
  #ifdef DEBUG      
    getDebugSerial()->println("Received data (IPD)");
  #endif      

  //ex +IPD,0,10:hi(32)again(13)
  //cbuf ,0,10:
  
  // serial buffer is at comma after D
  char* ipd = cbuf + COMMAND_LEN;
  
  // max channel + data length + 2 commas + colon = 9
  int len = readBytesUntil(ipd, ':', BUFFER_SIZE - COMMAND_LEN, 3000);
  
  if (len == 0) {
    // not found
    return -1;
  } else if (len == -1) {
    // timeout
    return -2;
  }

  // space ,0,1:(32)(13)(10)OK(13)(10)(13)(10)
  
  // parse channel
  // null term after channel for atoi
  ipd[2] = 0;
  int channel = atoi(ipd + 1);
  // reset
  ipd[2] = ',';
  
  #ifdef DEBUG
    getDebugSerial()->print("On channel "); 
    getDebugSerial()->println(channel);
  #endif
  
  //ipd[9] = 0;
  // length starts at pos 3
  len = atoi(ipd + 3);
  
  // subtract 2, don't want lf/cr
  len-=2;
  
  if (len <= 0) {
    #ifdef DEBUG
      getDebugSerial()->println("no data");
    #endif
    return -2; 
  }
  
  // reset so we can print
  
  #ifdef DEBUG
    getDebugSerial()->print("Data len "); 
    getDebugSerial()->println(len);
  #endif
  
  if (len > 128) {
    // error.. too large
    #ifdef DEBUG
      getDebugSerial()->println("Too long");
    #endif
    return -3;
  }

  // read input into data buffer
  int rlen = readChars(cbuf, 0, len, 3000);
  
  if (rlen == -1) {
    // timeout
    return -4;
  } else if (rlen != len) {
    #ifdef DEBUG
      getDebugSerial()->print("Data read failure "); 
      getDebugSerial()->println(rlen);            
    #endif
    return -4;
  }
 
  // null terminate
  cbuf[len] = 0;
  
  #ifdef DEBUG
    getDebugSerial()->print("Data:");     
    printCbuf(cbuf, len);
    getDebugSerial()->println("");  
  #endif
  
  return sendReply(channel, "ok");
}

int parseChannel(char *cbuf, bool open) {

  cbuf[1] = 0;
  int channel = atoi(cbuf);
  // replace char
  cbuf[1] = ',';
  
  if (channel >= 0 && channel <= 10) {
    connections[channel] = open;  
    return channel;  
  } else {
    return -1;
  }  
}

int handleConnected() {
  //0,CONNECT(13)(10)  
  int channel = parseChannel(cbuf, true);
      
  #ifdef DEBUG  
    getDebugSerial()->print("Connected on ");
    getDebugSerial()->println(channel);
  #endif

  //CONNECT_CMD_LEN - (COMMAND_LEN + 1)
  int len = readBytesUntil(cbuf + COMMAND_LEN, 10, CONNECT_CMD_LEN - COMMAND_LEN, 3000);
  
  if (len > 0) {
    return 1;
  } else {
    return len; 
  }
}

int handleClosed() {
  //0,CLOSED(13)(10)
  int channel = parseChannel(cbuf, false);

  #ifdef DEBUG  
    getDebugSerial()->print("Disconnected on ");
    getDebugSerial()->println(channel);
  #endif
  
  // consume rest of command
  int len = readBytesUntil(cbuf + COMMAND_LEN, 10, CLOSED_CMD_LEN - COMMAND_LEN, 3000);  
  
  if (len > 0) {    
    return 1;
  } else {
    return len; 
  }  
}

void loop() {
  //debugLoop();

  int result = 0;
  int command = 0;
  
  // the tricky part of AT commands is they vary in length and format, so we don't know how much to read without knowing what it is
  // There are no commands less than 6 chars and with 6 chars we should be able to identify all possible commands
  if (getEspSerial()->available() >= COMMAND_LEN) {
    #ifdef DEBUG
      getDebugSerial()->print("\n\nSerial available "); 
      getDebugSerial()->println(getEspSerial()->available());
    #endif
    
    int len = readChars(cbuf, 0, COMMAND_LEN, 1000);
    
    #ifdef DEBUG
      printCbuf(cbuf, COMMAND_LEN);
    #endif
    
    // not using Serial.find because if it doesn't match we lose the data. so not helpful

    if (strstr(cbuf, "+IPD") != NULL) {
      //(13)(10)+IPD,0,4:hi(13)(10)(13)(10)OK(13)(10) 
      result = handleData();
      command = IPD_COMMAND;
    } else if (strstr(cbuf, ",CONN") != NULL) {      
      //0,CONNECT(13)(10)      
      result = handleConnected();
      command = CONNECTED_COMMAND;
    } else if (strstr(cbuf, "CLOS") != NULL) {
      result = handleClosed();
      command = DISCONNECTED_COMMAND;     
    } else {
      #ifdef DEBUG
        getDebugSerial()->println("Unexpected..");
      #endif
      result = -1;
      command = DISCONNECTED_COMMAND;
      
      readFor(2000);
      
      // assume the worst and reset
      resetEsp8266();
    }

    #ifdef DEBUG
      if (result != 1) {
        getDebugSerial()->print("Loop error on command [");
        getDebugSerial()->print(command);
        getDebugSerial()->print("]: ");      
        getDebugSerial()->println(result);
      } else {
        getDebugSerial()->println("ok");
      }
    #endif
    
    if (false) {
      // health check        
      if (sendAt() != 1) {
        
      }
    }
    
    resetCbuf(cbuf, BUFFER_SIZE);    
  }
  
  //checkReset();  
}
