#include <SoftwareSerial.h>


// ideas
// this prog will fit in memory. load in memory to test 128 byte pages

// install atmega328 chip into diecimila and flash optiboot, or attempt to program the RBBB, or arduino pros
// buffer the pages so I can program 128 pages by collecting two 64 byte before sending
// only major difference now is page size

// moteino/dual optiboot seems to take the approach of using an external flash to write the program, then the bootloader reads from ext. flash an updates program https://github.com/LowPowerLab/DualOptiboot
// uses eeprom (soic package) http://www.digikey.com/product-detail/en/W25X40CLSNIG/W25X40CLSNIG-ND/3008652

// TODO send # of pages to expect in header

// boards.txt, baud rate, bootloader and more various boards
// /Applications/Arduino.app//Contents/Resources/Java/hardware/arduino/boards.txt

/*
impeeduino https://github.com/electricimp/reference/tree/master/hardware/impeeduino
socat virtual comm to socket http://stackoverflow.com/questions/22624653/create-a-virtual-serial-port-connection-over-tcp
optiloader (bootloader in sketch) https://github.com/WestfW/OptiLoader/blob/master/optiLoader.pde
adafruit optiloader https://github.com/adafruit/Standalone-Arduino-AVR-ISP-programmer
bootloader tips http://stackoverflow.com/questions/3652233/arduino-bootloader
sparkfun imp https://learn.sparkfun.com/tutorials/wireless-arduino-programming-with-electric-imp
intel hex http://en.wikipedia.org/wiki/Intel_HEX
https://code.google.com/p/arduino/source/browse/trunk/hardware/arduino/bootloaders/atmega/ATmegaBOOT_168.c
http://www.cs.ou.edu/~fagg/classes/general/atmel/avrdude.pdf
https://raw.githubusercontent.com/adafruit/ArduinoISP/master/ArduinoISP.ino
http://www.atmel.com/Images/doc2525.pdf
http://forum.arduino.cc/index.php?topic=117299.0;nowap
*/

// only need 128=> + 4 bytes len/addr
#define BUFFER_SIZE 150
#define READ_BUFFER_SIZE 150


#define STK_OK              0x10
#define STK_FAILED          0x11  // Not used
#define STK_UNKNOWN         0x12  // Not used
#define STK_NODEVICE        0x13  // Not used
#define STK_INSYNC          0x14  // ' '
#define STK_NOSYNC          0x15  // Not used
#define ADC_CHANNEL_ERROR   0x16  // Not used
#define ADC_MEASURE_OK      0x17  // Not used
#define PWM_CHANNEL_ERROR   0x18  // Not used
#define PWM_ADJUST_OK       0x19  // Not used
#define CRC_EOP             0x20  // 'SPACE'
#define STK_GET_SYNC        0x30  // '0'
#define STK_GET_SIGN_ON     0x31  // '1'
#define STK_SET_PARAMETER   0x40  // '@'
#define STK_GET_PARAMETER   0x41  // 'A'
#define STK_SET_DEVICE      0x42  // 'B'
#define STK_SET_DEVICE_EXT  0x45  // 'E'
#define STK_ENTER_PROGMODE  0x50  // 'P'
#define STK_LEAVE_PROGMODE  0x51  // 'Q'
#define STK_CHIP_ERASE      0x52  // 'R'
#define STK_CHECK_AUTOINC   0x53  // 'S'
#define STK_LOAD_ADDRESS    0x55  // 'U'
#define STK_UNIVERSAL       0x56  // 'V'
#define STK_PROG_FLASH      0x60  // '`'
#define STK_PROG_DATA       0x61  // 'a'
#define STK_PROG_FUSE       0x62  // 'b'
#define STK_PROG_LOCK       0x63  // 'c'
#define STK_PROG_PAGE       0x64  // 'd'
#define STK_PROG_FUSE_EXT   0x65  // 'e'
#define STK_READ_FLASH      0x70  // 'p'
#define STK_READ_DATA       0x71  // 'q'
#define STK_READ_FUSE       0x72  // 'r'
#define STK_READ_LOCK       0x73  // 's'
#define STK_READ_PAGE       0x74  // 't'
#define STK_READ_SIGN       0x75  // 'u'
#define STK_READ_OSCCAL     0x76  // 'v'
#define STK_READ_FUSE_EXT   0x77  // 'w'
#define STK_READ_OSCCAL_EXT 0x78  // 'x'

uint8_t cmd_buffer[1];
uint8_t buffer[BUFFER_SIZE];
uint8_t read_buffer[READ_BUFFER_SIZE];


// lots of serial data seems to crash leonardo
#define VERBOSE true

// wiring:
const int ssTx = 4;
const int ssRx = 5;
const int resetPin = 8;
// common ground
// 5V leonardo -> 5V diecimila

// leanardo boss (connected to usb)
// diecimila with optiboot is target

SoftwareSerial nss(ssTx, ssRx);

Stream* getProgrammerSerial() {
  return &Serial1;
}

Stream* getDebugSerial() {
  //nss now working
//  return &nss;  
  return &Serial;
}

void clear_read() {
  int count = 0;
  while (getProgrammerSerial()->available() > 0) {
    getProgrammerSerial()->read();
    count++;
  }
  
  if (count > 0) {
    getDebugSerial()->print("Discarded "); getDebugSerial()->print(count, DEC); getDebugSerial()->println(" extra bytes");
  }
}

int read_response(uint8_t len, int timeout) {
  long start = millis();
  int pos = 0;
  
//  if (VERBOSE) {
    //getDebugSerial()->print("read_response() expecting reply len: "); getDebugSerial()->println(len, DEC);    
//  }

  while (millis() - start < timeout) {
    
//    getDebugSerial()->println("waiting for response");
          
    if (getProgrammerSerial()->available() > 0) {
      read_buffer[pos] = getProgrammerSerial()->read();
      
      // extra verbose
//      if (VERBOSE) {
//        getDebugSerial()->print("read_response()<-"); getDebugSerial()->println(read_buffer[pos], HEX);        
//      }

      pos++;
      
      if (pos == len) {
        // we've read expected len
        break;
      }
    }
  }
  
  // consume any extra
  clear_read();
  
  if (pos == len) {
    return pos;
  }
  
  getDebugSerial()->print("read_response() timeout! read "); getDebugSerial()->print(pos, DEC); getDebugSerial()->print(" bytes but expected "); getDebugSerial()->print(len, DEC); getDebugSerial()->println(" bytes");
  return -1;
}

void dump_buffer(uint8_t arr[], char context[], uint8_t offset, uint8_t len) {
  getDebugSerial()->print(context);
  // weird this crashes leonardo
  //getDebugSerial()->print("start at "); getDebugSerial()->print(offset, DEC); getDebugSerial()->print(" len "); getDebugSerial()->print(len, DEC);
  getDebugSerial()->print(": ");
  
  for (int i = offset; i < offset + len; i++) {
    getDebugSerial()->print(arr[i], HEX);
    
    if (i < (offset + len) -1) {
      getDebugSerial()->print(",");
    }
  }
  
  getDebugSerial()->println("");
  getDebugSerial()->flush();
}

// Send command and buffer and return length of reply
int send(uint8_t command, uint8_t arr[], uint8_t offset, uint8_t len, uint8_t response_length) {

    if (VERBOSE) {
      if (command == STK_GET_PARAMETER) {
        getDebugSerial()->print("send() STK_GET_PARAMETER: "); getDebugSerial()->println(command, HEX);  
      } else if (command == STK_ENTER_PROGMODE) {
        getDebugSerial()->print("send() STK_ENTER_PROGMODE: "); getDebugSerial()->println(command, HEX);          
      } else if (command == STK_LEAVE_PROGMODE) {
        getDebugSerial()->print("send() STK_LEAVE_PROGMODE: "); getDebugSerial()->println(command, HEX);  
      } else if (command == STK_LOAD_ADDRESS) {
        getDebugSerial()->print("send() STK_LOAD_ADDRESS: "); getDebugSerial()->println(command, HEX);  
      } else if (command == STK_PROG_PAGE) {
        getDebugSerial()->print("send() STK_PROG_PAGE: "); getDebugSerial()->println(command, HEX);  
      } else if (command == STK_READ_PAGE) {
        getDebugSerial()->print("send() STK_READ_PAGE: "); getDebugSerial()->println(command, HEX);  
      } else if (command == STK_READ_SIGN) {
        getDebugSerial()->print("send() STK_READ_SIGN: "); getDebugSerial()->println(command, HEX);  
      } else {
        getDebugSerial()->print("send() unexpected command: "); getDebugSerial()->println(command, HEX);          
      }
    }
    
    //getProgrammerSerial()->write((char) command);
    getProgrammerSerial()->write(command);

  if (arr != NULL && len > 0) {
    for (int i = offset; i < offset + len; i++) {
      //getProgrammerSerial()->write((char) arr[i]);
      getProgrammerSerial()->write(arr[i]);

//      if (VERBOSE) {
//        getDebugSerial()->print("send()->"); getDebugSerial()->println(arr[i], HEX);  
//      }      
    }
    
    if (VERBOSE) {
      dump_buffer(arr, "send()->", offset, len);
    }
  }
  
  //getProgrammerSerial()->write((char) CRC_EOP);
  getProgrammerSerial()->write(CRC_EOP);
//  getProgrammerSerial()->flush();
      
  // add 2 bytes since we always expect to get back STK_INSYNC + STK_OK
  //int reply_len = read_response(response_length + 2, 5000);
  int reply_len = read_response(response_length + 2, 15000);

  if (reply_len == -1) {
    return -1;
  }
  
  if (VERBOSE) {
    dump_buffer(read_buffer, "send_reply", 0, reply_len);    
  }

  if (reply_len < 2) {
    getDebugSerial()->println("Invalid response");
    return -1; 
  }

  if (read_buffer[0] != STK_INSYNC) {
    getDebugSerial()->print("Expected STK_INSYNC but was "); getDebugSerial()->println(read_buffer[0], HEX);
    return -1;
  }
  
  if (read_buffer[reply_len - 1] != STK_OK) {
    getDebugSerial()->print("Expected STK_OK but was "); getDebugSerial()->println(read_buffer[reply_len - 1], HEX);
    return -1;    
  }
  
  // rewrite buffer without the STK_INSYNC and STK_OK
  uint8_t data_reply = reply_len - 2;
  
  for (int i = 0; i < data_reply; i++) {
    read_buffer[i] = read_buffer[i+1];
  }
  
  // zero the ok
  read_buffer[reply_len - 1] = 0;
  
  // return the data portion of the length
  return data_reply;
}

void bounce() {    
    // Bounce the reset pin
    // ported from tomatoless
    getDebugSerial()->println("Bouncing the Arduino reset pin");
//    delay(500);
    // set reset pin low
    digitalWrite(resetPin, LOW);
    delay(200);
    digitalWrite(resetPin, HIGH);
    delay(300);
    //delay(10);
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
    
    getDebugSerial()->print("Firmware version is "); getDebugSerial()->println(read_buffer[0], HEX);
    
    cmd_buffer[0] = 0x82;
    data_len = send(STK_GET_PARAMETER, cmd_buffer, 0, 1, 1);

    if (data_len == -1) {
     return -1;   
    }

    getDebugSerial()->print("Minor is "); getDebugSerial()->println(read_buffer[0], HEX);    
    
    // this not a valid command. optiboot will send back 0x3 for anything it doesn't understand
    cmd_buffer[0] = 0x83;
    data_len = send(STK_GET_PARAMETER, cmd_buffer, 0, 1, 1);
    
    if (data_len == -1) {
      return -1;   
    } else if (read_buffer[0] != 0x3) {
      getDebugSerial()->print("Expected 0x3 but was "); getDebugSerial()->println(read_buffer[0]);    
      return -1;
    }

    data_len = send(STK_READ_SIGN, NULL, 0, 0, 3);
    
    if (data_len != 3) {      
      return -1;      
      // tomatoless expects a different signature than what I get from optiboot so this might not be effective verification
      // assert(signature.len() == 3 && signature[0] == 0x1E && signature[1] == 0x95 && signature[2] == 0x0F);
    } else if (read_buffer[0] != 0x1E && read_buffer[1] != 0x94 && read_buffer[2] != 0x6) {
      getDebugSerial()->println("Signature invalid");
      return -1;
    }
    
    // TODO
    // avrdude does a set device
    //avrdude: Send: B [42] . [86] . [00] . [00] . [01] . [01] . [01] . [01] . [03] . [ff] . [ff] . [ff] . [ff] . [00] . [80] . [02] . [00] . [00] . [00] @ [40] . [00]   [20]     
    // then set device ext
    //avrdude: Send: E [45] . [05] . [04] . [d7] . [c2] . [00]   [20]     
    return 0;
}

int send_page(uint8_t addr_offset, uint8_t data_len) {    
    // ctrl,len,addr high/low
    // address is byte index 2,3
    
    // [55] . [00] . [00] 
    if (send(STK_LOAD_ADDRESS, buffer, addr_offset, 2, 0) == -1) {
      getDebugSerial()->println("load addr failed");
      return -1;
    }
     
    // [64] . [00] . [80] F [46] . [0c] .
    
    // rewrite buffer to make things easier
    // data starts at addr_offset + 2
    // format of prog_page is 0x00, data_len, 0x46, data
    buffer[addr_offset - 1] = 0;
    buffer[addr_offset] = data_len;
    
    //WTF avrdude doesn't send this for optiboot 5
    buffer[addr_offset + 1] = 0x46;
    //remaining buffer is data
    
    // send page. len data + command bytes
    if (send(STK_PROG_PAGE, buffer, addr_offset - 1, data_len + 3, 0) == -1) {
      getDebugSerial()->println("page page failed");
      return -1;       
    }

    uint8_t reply_len = send(STK_READ_PAGE, buffer, addr_offset - 1, 3, data_len);
    
    if (reply_len == -1) {
      getDebugSerial()->println("Read page failure");
      return -1;
    }
    
    if (reply_len != data_len) {
      getDebugSerial()->println("Error: read len does not match data len");
      return -1;
    }
    
    bool verified = true;
    
    // TODO we can compute checksum on buffer, reset and use for the read buffer!!!!!!!!!!!!!!!
    
    if (VERBOSE) {
      getDebugSerial()->print("reply_len is "); getDebugSerial()->println(reply_len, DEC);      
    }
      
    for (int i = 0; i < reply_len; i++) {        
      if (read_buffer[i] != buffer[addr_offset + 2 + i]) {
        getDebugSerial()->print("Error: reply buffer does not match write buffer at "); getDebugSerial()->println(i, DEC);
        return -1;
      }
    }
    
    return 0;
}

void setup() {
  Serial.begin(9600);
  // leonardo wait for serial
  while (!Serial);
  
  pinMode(resetPin, OUTPUT);
  
  //diecimilao.upload.maximum_size=15872
  //diecimilao.upload.speed=115200

  // configure serial for bootloader baud rate  
  Serial1.begin(115200);
  // fail
  //  Serial1.begin(19200);

  nss.begin(9600);
}

uint8_t page_len = 0;
//uint8_t data_len = 0;
uint8_t pos = 0;
uint8_t high = 0;
uint8_t low = 0;

int count = 0;
bool prog_mode = false;
bool is_first_page = false;
bool is_last_page = false;

// called after each page is completed
void pageReset() {
  pos = 0;
  page_len = 0;
  is_first_page = false;
  is_last_page = false;  
}

// called after programming completes
void progReset() {
  pageReset();
  prog_mode = false;
}

const int FIRST_PAGE = 0xd;
const int LAST_PAGE = 0xf;
const int PROG_PAGE = 0xa;

void loop() {
  
  int b = 0;

  // each page is ctrl,len,high,low,data

  // receive a page at a time, ex
  // f,80,e,94,9c,7,8,95,fc,1,16,82,17,82,10,86,11,86,12,86,13,86,14,82,34,96,bf,1,e,94,bd,7,8,95,dc,1,68,38,10,f0,68,58,29,c0,e6,2f,f0,e0,67,ff,13,c0,e0,58,f0,40,81,e0,90,e0,2,c0,88,f,99,1f, data is e,94,9c,7,8,95,fc,1,16,82,17,82,10,86,11,86,12,86,13,86,14,82,34,96,bf,1,e,94,bd,7,8,95,dc,1,68,38,10,f0,68,58,29,c0,e6,2f,f0,e0,67,ff,13,c0,e0,58,f0,40,81,e0,90,e0,2,c0,88,f,99,1fe,44,f,80,e,94,9c,7,8,95,fc,1,16,82,17,82,10,86,11,86,12,86,13,86,14,82,34,96,bf,1,e,94,bd,7,8,95,dc,1,68,38,10,f0,68,58,29,c0,e6,2f,f0,e0,67,ff,13,c0,e0,58,f0,40,81,e0,90,e0,2,c0,88,f,99,1f,  
  while (getDebugSerial()->available() > 0) {
    b = getDebugSerial()->read();
    
//    Serial.print("loop()<- data is "); Serial.println(b, HEX);
  
    
    if (pos == 0) {
      // has nothing to do with programming, don't need this in buffer
      buffer[pos] = b;
            
      if (b == FIRST_PAGE) {
        prog_mode = true;
        is_first_page = true;
      } else if (b == LAST_PAGE) {
        is_last_page = true;
      }
    } else if (pos == 1 && prog_mode) {
      // length
      // length is only the data length,  so add 4 byte (ctrl, len, addr high, low)
      buffer[pos] = b + 4;
      page_len = buffer[pos];      
      getDebugSerial()->print("page len is "); getDebugSerial()->println(b, DEC);
    } else if (pos < page_len - 1 && prog_mode) {
      // data
      buffer[pos] = b;
    } else if (pos == (page_len - 1) && prog_mode) {
      // complete page
      buffer[pos] = b;
      
      if (is_first_page) {
        // first page, reset the target and perform check
        bounce();
        
        if (check_duino() != 0) {
          getDebugSerial()->println("Check failed!"); 
          progReset();
          continue;
        } 
        
        if (send(STK_ENTER_PROGMODE, buffer, 0, 0, 0) == -1) {
          getDebugSerial()->println("STK_ENTER_PROGMODE failure");
          progReset();
          continue;            
        }        
      }
      
      if (VERBOSE) {
        dump_buffer(buffer, "prog_page", 0, page_len);        
      }

      if (send_page(2, page_len - 4) != -1) {
        // send ok after each page so client knows to send another
        getDebugSerial()->println("ok");       
      } else {
        getDebugSerial()->println("Send page failure"); 
        progReset();      
        continue;         
      }
      
      if (is_last_page) {
        // done. leave prog mode
        if (send(STK_LEAVE_PROGMODE, buffer, 0, 0, 0) == -1) {
          getDebugSerial()->println("STK_LEAVE_PROGMODE failure");
          progReset();
          continue;            
        }
        
        progReset();      
        continue; 
      } else {
        pageReset();  
        continue;      
      }
    }
     
    pos++;
    
    // TODO pos > 0 && !prog_mode
    if (pos >= BUFFER_SIZE) {
      getDebugSerial()->println("Error read past buffer");
      progReset();
    }
  }
  
  // oops, got some data we are not expecting
  if (getProgrammerSerial()->available() > 0) {
    uint8_t ch = getProgrammerSerial()->read();
    getDebugSerial()->print("Unexpected reply @"); getDebugSerial()->print(count, DEC); getDebugSerial()->print(" "); getDebugSerial()->println(ch, HEX);
    count++;
  }
}
