

/*
- Power decimila via external power to free serial?
- usb serial issue drops bytes around position 90. 128 seems to be the page size 
- Try optiboot if no success with origin bootloader
- look at other bootloaders



Nordic
not full duplex, should be ok
limited range. can buy with external antenna for ~6
power low
packets up to 25 bytes
cheap!! ~$3
spi interface
3.3V (5v tolerant?)
Arduino library

Wifi ESP8266
power hungry - should work ok with arduino power
inconvenient pin out.. need adapter
requires serial port with default baud rate
must be able to change baud rate to use with softserial
wifi, so doesn’t require tx/rx pair!
cheap ~$3
seems to require wifi pass in the sketch, ugh
AT commands
Instructable for setting 9600 br http://www.instructables.com/id/ESP8266-Wifi-Temperature-Logger/


XBee
works well
not cheap
low power

impeeduino https://github.com/electricimp/reference/tree/master/hardware/impeeduino
socat virtual comm to socket http://stackoverflow.com/questions/22624653/create-a-virtual-serial-port-connection-over-tcp
bootloader tips http://stackoverflow.com/questions/3652233/arduino-bootloader
sparkfun imp https://learn.sparkfun.com/tutorials/wireless-arduino-programming-with-electric-imp
intel hex http://en.wikipedia.org/wiki/Intel_HEX
https://code.google.com/p/arduino/source/browse/trunk/hardware/arduino/bootloaders/atmega/ATmegaBOOT_168.c
http://www.cs.ou.edu/~fagg/classes/general/atmel/avrdude.pdf
https://raw.githubusercontent.com/adafruit/ArduinoISP/master/ArduinoISP.ino
http://www.atmel.com/Images/doc2525.pdf
http://forum.arduino.cc/index.php?topic=117299.0;nowap

*/

// 128 + 3 bytes len/addr
#define BUFFER_SIZE 150
#define READ_BUFFER_SIZE 150

#define CRC_EOP 0x20
#define STK_LOAD_ADDRESS 0x55
#define STK_PROG_PAGE 0x64
#define STK_READ_PAGE 0x74
#define STK_GET_PARAMETER 0x41
#define STK_READ_SIGN 0x75
#define STK_OK 0x10
#define STK_INSYNC 0x14

byte cmd_buffer[1];
byte buffer[BUFFER_SIZE];
byte read_buffer[READ_BUFFER_SIZE];

//const int ssTx = 4;
//const int ssRx = 5;
const int resetPin = 8;

// structure
// data_len = len - 3
// len,addr high, addr low,data

// TODO start byte + escaping and checksum

byte len = 0;
byte data_len = 0;
byte pos = 0;
byte high = 0;
byte low = 0;

void clear_read() {
  while (getProgrammerSerial()->read() != -1) {
    Serial.println("Extra bytes on input buffer!");  
  }
}

HardwareSerial* getProgrammerSerial() {
  return &Serial1;
}

//HardwareSerial* getDebugSerial() {
//  return &Serial;
//}

int read_response(byte len, int timeout) {
  long start = millis();
  int pos = 0;
  
  Serial.print("read_response() expecting reply len: "); Serial.println(len, HEX);
  
  while (millis() - start < timeout) {
    
//    Serial.println("waiting for response");
          
    if (getProgrammerSerial()->available() > 0) {
      read_buffer[pos] = getProgrammerSerial()->read();
      Serial.print("read_response()<-"); Serial.println(read_buffer[pos], HEX);

      pos++;
      
      if (pos == len) {
        // we've read expected len
        Serial.println("response complete");
        break;
      }
    }
  }
  
  // consume an extra
  clear_read();
  
  if (pos == len) {
    Serial.print("read_response() success");
    // success
    return pos;
  }
  
  Serial.print("read_response() fail read "); Serial.print(pos, DEC); Serial.println(" bytes");
  return -1;
}

void dump_buffer(byte arr[], char context[], byte len) {
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

// Send command and buffer and return length of reply
int send(byte command, byte arr[], byte offset, byte len, byte response_length) {

  Serial.print("send() command "); Serial.println(command, HEX);
  getProgrammerSerial()->write(command);
  
  if (arr != NULL) {
    for (int i = offset; i < offset + len; i++) {
      getProgrammerSerial()->print(arr[i], HEX);
      Serial.print("send()->"); Serial.println(arr[i], HEX);
    }
  }
  
  getProgrammerSerial()->print(CRC_EOP, HEX);
  getProgrammerSerial()->flush();
  
  Serial.print("send() CRC_EOP "); Serial.println(CRC_EOP, HEX);
      
  // add 2 bytes since we always expect to get back STK_INSYNC + STK_OK
  int reply_len = read_response(response_length + 2, 5000);

  if (reply_len == -1) {
    return -1;
  }
  
  dump_buffer(read_buffer, "send_reply", reply_len);

  if (reply_len < 2) {
    Serial.println("Invalid response");
    return -1; 
  }

  if (read_buffer[0] != STK_INSYNC) {
    Serial.print("Expected STK_INSYNC but was"); Serial.println(read_buffer[0], HEX);
    return -1;
  }
  
  if (read_buffer[reply_len - 1] != STK_OK) {
    Serial.println("Expected STK_OK");
    return -1;    
  }
  
  // rewrite buffer without default 2 bytes
  
  byte data_reply = reply_len - 2;
  
  for (int i = 0; i < data_reply; i++) {
    read_buffer[i] = read_buffer[i+2];
  }
  
  // return the data portion of the length
  return data_reply;
}

void bounce() {    
    // Bounce the reset pin
    // ported from tomatoless
    Serial.println("Bouncing the Arduino reset pin");
    delay(500);
    // set reset pin low
    digitalWrite(resetPin, LOW);
    delay(200);
    digitalWrite(resetPin, HIGH);
    delay(300);
}

int check_duino() {
  clear_read();
    
    int data_len = 0;
    
    // Check everything we can check to ensure we are speaking to the correct boot loader
    cmd_buffer[0] = 0x81;
    data_len = send(STK_GET_PARAMETER, cmd_buffer, 0, 1, 1);
    
    if (data_len == -1) {
     return -1;   
    }
    
    Serial.print("Major is"); Serial.println(read_buffer[0]);
    
    cmd_buffer[0] = 0x82;
    data_len = send(STK_GET_PARAMETER, cmd_buffer, 0, 1, 1);

    if (data_len == -1) {
     return -1;   
    }

    Serial.print("Minor is"); Serial.println(read_buffer[0]);    
    
    cmd_buffer[0] = 0x83;
    data_len = send(STK_GET_PARAMETER, cmd_buffer, 0, 1, 1);
    
    if (data_len == -1) {
      return -1;   
    } else if (read_buffer[0] != 0x3) {
      Serial.print("Expected 0x3 but instead was "); Serial.println(read_buffer[0]);    
      return -1;
    }
    
    // weird tomatoless has 0 response bytes
    send(STK_READ_SIGN, NULL, 0, 0, 3);
    
    if (data_len != 3) {
      return -1;      
    } else if (read_buffer[0] != 0x1E && read_buffer[1] != 0x95 && read_buffer[2] != 0x0F) {
      Serial.print("Signature invalid");
      return -1;
    }
}

int send_chunk() {    
    send(STK_LOAD_ADDRESS, buffer, 1, 2, 0);
    
    byte data_len = len - 3;
    
    // now overwrite addr to reuse buffer
    buffer[0] = 0;
    buffer[1] = data_len;
    buffer[2] = 0x46;
    //remaining buffer is data
    
    // send page
    send(STK_PROG_PAGE, buffer, 0, len, 0);
    
    // now read it back??
    // response length is always + 2
    byte reply_len = send(STK_READ_PAGE, buffer, 0, len, data_len);
    
    if (reply_len != data_len) {
      Serial.println("Error: read len does not match data len");
      return -1;
    }
    
    bool verified = true;
    
    // TODO we can compute checksum on buffer, reset and use for the read buffer!!!!!!!!!!!!!!!
    
    for (int i = 0; i < data_len; i++) {
      if (read_buffer[i] != buffer[i]) {
        Serial.println("Error: reply buffer does not match write buffer at " + i);
        return -1;
      }
    } 
}

void setup() {
  Serial.begin(9600);
  // leonardo wait for serial
  while (!Serial);

  Serial.println("Waiting for sketch");
  
  pinMode(resetPin, OUTPUT);
  
  // configure serial for bootloader baud rate  
  Serial1.begin(19200);
}
    
void reset() {
  pos = 0;
  len = 0;
}

long last = 0;

void loop() {
  
  int b = 0;
  
  // each program is ctrl,len,high,low,data
  // how do we know when we get the last packet? we dont!
  
  while (Serial.available() > 0) {
    b = Serial.read();
    
    if (pos == 0) {
      if (b == 1) {
        bounce();
      } else if (b == 2) {
        Serial.println("done");  
      }
    } else if (pos == 1) {
      Serial.print("Len is "); Serial.println(b, HEX);
      buffer[pos] = b;
      len = b;      
    } else if (pos == 2) {
      buffer[pos] = b;
      //Serial.print("addr high byte is "); Serial.println(byte, HEX);
    } else if (pos == 3) {
      buffer[pos] = b;
      //Serial.print("addr low byte is "); Serial.println(byte, HEX);      
    } else if (pos < len) {
      // data
      buffer[pos] = b;
      //Serial.print("k pos "); Serial.print(pos); Serial.print(" "); Serial.println(b, HEX);
      //Serial.print("data is "); Serial.print(byte, HEX); Serial.print(", pos is ");  Serial.print(pos); Serial.print(", len is "); Serial.println(len);
      
      if (pos == len - 1) {
        // last byte in packet
        check_duino();        
//        dump_buffer(buffer, "Processed chunk from host", len);
        //Serial.println("ok");
        //send_chunk();
        reset();
      
        continue;        
      }
    }
     
    pos++;
    
    if (pos >= BUFFER_SIZE) {
      Serial.println("Error read past buffer");
      reset();
    }
  }
  
  if (millis() - last > 1000) {
    Serial.println("No serial available");
    last = millis();
  }
  
}
