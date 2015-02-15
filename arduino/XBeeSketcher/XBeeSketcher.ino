#include <Wire.h>
#include <extEEPROM.h>
#include <SoftwareSerial.h>
#include <XBee.h>

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

#define MAGIC_BYTE1 0xef
#define MAGIC_BYTE2 0xac

#define START_PROGRAMMING 0xa0
#define PROGRAM_DATA 0xa1
#define STOP_PROGRAMMING 0xa2

#define CONTROL_PROG_REQUEST 0x10
#define CONTROL_PROG_DATA 0x20
#define CONTROL_FLASH_START 0x40

#define OK 1
#define FAILURE 2

// SCRATCH -----------------------> finally success 2/15/15 10:12AM: Flash in 2174ms!

// due to my flagrant use of Serial.println, the leonardo will go out of sram if version is true :(
#define VERBOSE false

// WIRING:
// unfortunately we can use an xbee shield because we need the serial port for programming. gotta use XBee with softserial
// optiboot needs 115.2 so softserial is not an option
// consider flashing optiboot @ 19.2 so softserial is viable

// another configuration that might work if the target is wired to the radio: it writes to the eeprom then tells it's friend to program it. this only works if two arduinos can be on the same i2c bus

// WHY THIS?? lots of reasons but ultimately we are not really wireless if we need to plug our projects into a cable to program them.
// works with any speed wireless, does not need to match the bootloader baud rate or timeout

// TROUBLESHOOTING. 
// if flash_init fails with 0,0,0 response you are not talking to the bootloader, verify the resetPin is connected to Reset on the target. Also verify Serial1 (UART) is at 115200

const int softTxPin = 11;
const int softRxPin = 12;
const int resetPin = 8;

const int PROG_PAGE_RETRIES = 2;
const int EEPROM_OFFSET_ADDRESS = 16;
// max time between optiboot commands before we send a noop.. not so relevant when using eeprom
const int MAX_OPTI_DELAY = 300;
// if we don't receive a packet every X ms, timeout
const long XBEE_TIMEOUT = 5000;

XBee xbee = XBee();
XBeeResponse response = XBeeResponse();
ZBRxResponse rx = ZBRxResponse();

uint8_t payload[] = { 0 };
uint32_t COORD_MSB_ADDRESS = 0x0013a200;
uint32_t COORD_LSB_ADDRESS = 0x408b98fe;

// Coordinator/XMPP Gateway
XBeeAddress64 addr64 = XBeeAddress64(COORD_MSB_ADDRESS, COORD_LSB_ADDRESS);
ZBTxRequest tx = ZBTxRequest(addr64, payload, sizeof(payload));
ZBTxStatusResponse txStatus = ZBTxStatusResponse();

uint8_t cmd_buffer[1];
uint8_t addr[2];
uint8_t buffer[BUFFER_SIZE];
uint8_t read_buffer[READ_BUFFER_SIZE];

long last_optiboot_cmd = 0;
int packet_count = 0;
int num_packets = 0;
int prog_size = 0;
bool in_prog = false;
long prog_start = 0;
long last_packet = 0;
int current_eeprom_address = EEPROM_OFFSET_ADDRESS;
bool in_bootloader = false;

/*
Microchip 24LC256
Arduino Analog SDA - EEPROM pin 5
Arduino Analog SCL - EEPROM pin 6
Arduino 5V - VCC - EEPROM pin 8
Arduino GND - VSS - EEPROM pin 4 

* Connect pins 1, 2, and 3 of the eeprom GND. Pin 7 (write protect) should also be connected to GND according to datasheet but I found it works fine being open
* pin 1 is has the dot, on the notched end, if you were wondering ;)

See Arduino for how to find I2C pins for your board, it varies:
Board I2C / TWI pins
Uno, Ethernet A4 (SDA), A5 (SCL)
Mega2560  20 (SDA), 21 (SCL)
Leonardo  2 (SDA), 3 (SCL)
Due 20 (SDA), 21 (SCL), SDA1, SCL1
*/


//Since Arduino 1.0 we have the superior softserial implementation: NewSoftSerial
// Remember to connect all devices to a common Ground: XBee, Arduino and USB-Serial device
SoftwareSerial nss(softTxPin, softRxPin);

extEEPROM eeprom(kbits_256, 1, 64);

Stream* getProgrammerSerial() {
  return &Serial1;
}

Stream* getXBeeSerial() {
  return &nss;  
}

Stream* getDebugSerial() {
  return &Serial;  
}

void clear_read() {
  int count = 0;
  while (getProgrammerSerial()->available() > 0) {
    getProgrammerSerial()->read();
    count++;
  }
  
  if (count > 0) {
    if (VERBOSE) {
      getDebugSerial()->print("clear_read: trashed "); getDebugSerial()->print(count, DEC); getDebugSerial()->println(" bytes");      
    }
  }
}

// returns bytes read, -1 if error
int read_response(uint8_t len, int timeout) {
  long start = millis();
  int pos = 0;

  while (millis() - start < timeout) {          
    if (getProgrammerSerial()->available() > 0) {
      read_buffer[pos] = getProgrammerSerial()->read();
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
  
  // TODO return error code instead of strings that take up precious memeory
  getDebugSerial()->print("read timeout! got "); getDebugSerial()->print(pos, DEC); getDebugSerial()->print(" byte, expected "); getDebugSerial()->print(len, DEC); getDebugSerial()->println(" bytes");
  return -1;
}

void dump_buffer(uint8_t arr[], char context[], uint8_t len) {
  getDebugSerial()->print(context);
  getDebugSerial()->print(": ");
  
  for (int i = 0; i < len; i++) {
    getDebugSerial()->print(arr[i], HEX);
    
    if (i < len -1) {
      getDebugSerial()->print(",");
    }
  }
  
  getDebugSerial()->println("");
  getDebugSerial()->flush();
}

void update_last_command() {
  last_optiboot_cmd = millis();  
}

// Send command and buffer and return length of reply
int send(uint8_t command, uint8_t *arr, uint8_t len, uint8_t response_length) {

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
      } else if (command == STK_GET_SYNC) {
        getDebugSerial()->print("send() STK_GET_SYNC: "); getDebugSerial()->println(command, HEX);  
      } else {
        getDebugSerial()->print("send() unexpected command: "); getDebugSerial()->println(command, HEX);          
      }
    }
    
    getProgrammerSerial()->write(command);

  if (arr != NULL && len > 0) {
    for (int i = 0; i < len; i++) {
      getProgrammerSerial()->write(arr[i]);   
    }
    
    if (VERBOSE) {
      dump_buffer(arr, "send()->", len);
    }
  }
  
  getProgrammerSerial()->write(CRC_EOP);
  // make it synchronous
  getProgrammerSerial()->flush();
      
  // add 2 bytes since we always expect to get back STK_INSYNC + STK_OK
  int reply_len = read_response(response_length + 2, 5000);

  if (reply_len == -1) {
    return -1;
  }
  
  if (VERBOSE) {
    dump_buffer(read_buffer, "send_reply", reply_len);    
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
  
  // success update
  update_last_command();
  
  // return the data portion of the length
  return data_reply;
}

int flash_init() {
  clear_read();
    
    int data_len = 0;
    
    cmd_buffer[0] = 0x81;
    data_len = send(STK_GET_PARAMETER, cmd_buffer, 1, 1);
    
    if (data_len == -1) {
     return -1;   
    }
    
    if (VERBOSE) {
      getDebugSerial()->print("Firmware: "); getDebugSerial()->println(read_buffer[0], HEX);      
    }

    
    cmd_buffer[0] = 0x82;
    data_len = send(STK_GET_PARAMETER, cmd_buffer, 1, 1);

    if (data_len == -1) {
     return -1;   
    }

    if (VERBOSE) {
      getDebugSerial()->print("Minor: "); getDebugSerial()->println(read_buffer[0], HEX);    
    }
    
    // this not a valid command. optiboot will send back 0x3 for anything it doesn't understand
    cmd_buffer[0] = 0x83;
    data_len = send(STK_GET_PARAMETER, cmd_buffer, 1, 1);
    
    if (data_len == -1) {
      return -1;   
    } else if (read_buffer[0] != 0x3) {
      getDebugSerial()->print("Unxpected optiboot reply: "); getDebugSerial()->println(read_buffer[0]);    
      return -1;
    }

    data_len = send(STK_READ_SIGN, NULL, 0, 3);
    
    if (data_len != 3) {      
      return -1;      
    } else if (read_buffer[0] != 0x1E && read_buffer[1] != 0x94 && read_buffer[2] != 0x6) {
      getDebugSerial()->println("Signature invalid");
      return -1;
    }
    
    // IGNORED BY OPTIBOOT
    // avrdude does a set device
    //avrdude: Send: B [42] . [86] . [00] . [00] . [01] . [01] . [01] . [01] . [03] . [ff] . [ff] . [ff] . [ff] . [00] . [80] . [02] . [00] . [00] . [00] @ [40] . [00]   [20]     
    // then set device ext
    //avrdude: Send: E [45] . [05] . [04] . [d7] . [c2] . [00]   [20]     
    return 0;
}

// need 3 bytes for prog_page so data should start at buf[3]
int send_page(uint8_t *addr, uint8_t *buf, uint8_t data_len) {    
    // ctrl,len,addr high/low
    // address is byte index 2,3
    
    // retry up to 2 times
    // disable retries for debugging
    //for (int z = 0; z < PROG_PAGE_RETRIES + 1; z++) {
      // [55] . [00] . [00] 
      if (send(STK_LOAD_ADDRESS, addr, 2, 0) == -1) {
        getDebugSerial()->println("Load address failed");
        return -1;
      }
       
      // [64] . [00] . [80] F [46] . [0c] .
      
      // rewrite buffer to make things easier
      // data starts at addr_offset + 2
      // format of prog_page is 0x00, data_len, 0x46, [data]
      buffer[0] = 0;
      buffer[1] = data_len;
      buffer[2] = 0x46;
      //remaining buffer is data
      
      // add 3 to data_len
      if (send(STK_PROG_PAGE, buffer, data_len + 3, 0) == -1) {
        getDebugSerial()->println("Prog page failed");
        return -1;
      }
  
      uint8_t reply_len = send(STK_READ_PAGE, buffer, 3, data_len);
      
      if (reply_len == -1) {
        getDebugSerial()->println("Read page failure");
        return -1;
      }
      
      if (reply_len != data_len) {
        getDebugSerial()->println("Read page len fail");
        return -1;
      }
      
      // TODO we can compute checksum on buffer, reset and use for the read buffer!!!!!!!!!!!!!!!
      
      if (VERBOSE) {
        getDebugSerial()->print("Read page length is "); getDebugSerial()->println(reply_len, DEC);      
      }
      
      bool verified = true;
      
      // verify each byte written matches what is returned by bootloader
      for (int i = 0; i < reply_len; i++) {        
        if (read_buffer[i] != buffer[i + 3]) {
          getDebugSerial()->print("Verify page fail @ "); getDebugSerial()->println(i, DEC);
          verified = false;
          break;
        }
      }
      
      if (!verified) {
        // retry is still attempts remaining
        //if (z < PROG_PAGE_RETRIES) {
        //  getDebugSerial()->println("Failed to verify page.. retrying");
        //} else {
          getDebugSerial()->println("Verify page fail");
        //}
        
        // disable retries
        return -1;
        //continue;
      }
      
      return 0;      
    //}
    
  // read verify failure
  return -1;
}


// called after programming completes
void prog_reset() {
  last_optiboot_cmd = 0;
  packet_count = 0;
  num_packets = 0;
  prog_size = 0;
  in_prog = false;
  prog_start = 0;
  last_packet = 0;
  current_eeprom_address = EEPROM_OFFSET_ADDRESS;
}

void forwardPacket() {
  // not programming packet, so proxy all xbee traffic to Arduino
  // prob cleaner way to do this if I think about it some more
  
  // send start byte, length, api, then frame data + checksum
  sendByte(0x7d, false);
  sendByte(xbee.getResponse().getMsbLength(), true);
  sendByte(xbee.getResponse().getLsbLength(), true);        
  sendByte(xbee.getResponse().getApiId(), true);
   
  uint8_t* frameData = xbee.getResponse().getFrameData();
   
   for (int i = 0; i < xbee.getResponse().getFrameDataLength(); i++) {
  sendByte(*(frameData + i), true);
   }
   
   sendByte(xbee.getResponse().getChecksum(), true);  
}


// borrowed from xbee api. send bytes with proper escaping
void sendByte(uint8_t b, bool escape) {
  if (escape && (b == START_BYTE || b == ESCAPE || b == XON || b == XOFF)) {
    getProgrammerSerial()->write(ESCAPE);
    getProgrammerSerial()->write(b ^ 0x20);
  } else {
    getProgrammerSerial()->write(b);
  }
  
  getProgrammerSerial()->flush();
}

int flash(int start_address, int size) {
  // now read from eeprom and program
  getDebugSerial()->println("Flashing from eeprom...");
  
  long start = millis();
  
  bounce();
  
  if (flash_init() != 0) {
    getDebugSerial()->println("Check failed!"); 
    prog_reset();
    return -1;
  } 
  
  if (send(STK_ENTER_PROGMODE, buffer, 0, 0) == -1) {
    getDebugSerial()->println("STK_ENTER_PROGMODE failure");
    return -1;
  }    
  
  int current_address = start_address;
  
  while (current_address < size) {
    int len = 0;
    
    if (size - current_address < 128) {
      len = size - current_address;
    } else {
      len = 128;
    }
    
    if (VERBOSE) {
      getDebugSerial()->print("EEPROM read at address "); getDebugSerial()->print(current_address, DEC); getDebugSerial()->print(" len is "); getDebugSerial()->println(len, DEC);            
    }
     
    // skip 2 so we leave room for the prog_page command
    int ok = eeprom.read(current_address, buffer + 3, len);
    
    if (ok != 0) {
      getDebugSerial()->println("EEPROM read fail");
      return -1;
    }
    
    // set laod address little endian
    // gotta divide by 2
    addr[0] = ((current_address - start_address) / 2) & 0xff;
    addr[1] = (((current_address - start_address) / 2) >> 8) & 0xff;
    
    if (VERBOSE) {
      getDebugSerial()->print("Sending page len: "); getDebugSerial()->println(len, DEC);            
      dump_buffer(buffer + 3, "read from eeprom->", len);
    }
                  
    // flash
    if (send_page(addr, buffer, len) == -1) {
      getDebugSerial()->println("Send page fail");
      return -1;
    }
    
    current_address+= len;
  }
  
  if (send(STK_LEAVE_PROGMODE, buffer, 0, 0) == -1) {
    getDebugSerial()->println("STK_LEAVE_PROGMODE failure");
    return -1;       
  }
  
  getDebugSerial()->print("Flash in "); getDebugSerial()->print(millis() - start, DEC); getDebugSerial()->println("ms");
  getDebugSerial()->println("ok");
  
  return 0;
}

void bounce() {    
    //clear_read();

    // Bounce the reset pin
    getDebugSerial()->println("Bouncing the Arduino");
    // set reset pin low
    digitalWrite(resetPin, LOW);
    delay(200);
    digitalWrite(resetPin, HIGH);
    delay(300);
    in_bootloader = true;
}

int sendMessageToProgrammer(uint8_t status) {
  payload[0] = status;
  // TODO set frame id with millis & 256
  xbee.send(tx);
  
  // after sending a tx request, we expect a status response
  // wait up to half second for the status response
  if (xbee.readPacket(1000)) {    
    if (xbee.getResponse().getApiId() == ZB_TX_STATUS_RESPONSE) {
      xbee.getResponse().getZBTxStatusResponse(txStatus);

      // get the delivery status, the fifth byte
      if (txStatus.isSuccess()) {
        // good
        return 0;
      } else {
        getDebugSerial()->println("TX fail");
      }
    }      
  } else if (xbee.getResponse().isError()) {
    getDebugSerial()->print("TX error:");  
    getDebugSerial()->print(xbee.getResponse().getErrorCode());
  } else {
    getDebugSerial()->println("TX timeout");  
  } 
  
  return -1;
}
        
void handlePacket() {
    // if programming start magic packet is received:
    // reset the target arduino.. determine the neecessary delay
    // send data portion of packets to serial.. but look for magic word on packet so we know it's programming data
    // NOTE: any programs that send to this radio should be shutdown or the programming would hose the arduino
    // on final packet do any verification to see if it boots

    if (xbee.getResponse().getApiId() == ZB_RX_RESPONSE) {
      
      getDebugSerial()->println("RX");
      
      // now fill our zb rx class
      xbee.getResponse().getZBRxResponse(rx);
      
      // 3 bytes for head + at least one programming
      if (rx.getDataLength() >= 4 && rx.getData(0) == MAGIC_BYTE1 && rx.getData(1) == MAGIC_BYTE2) {          
        if (rx.getData(2) == CONTROL_PROG_REQUEST) {
         // start

          getDebugSerial()->println("Start");
          
          if (in_prog) {
            getDebugSerial()->println("Error: in prog"); 
            // TODO send error to client
            return;
          }

          // TODO tell Arduino it's about to be flashed
          // forwardPacket();
          
          prog_start = millis();
          in_prog = true; 
          packet_count = 0;
          
          // size in bytes
          prog_size = (rx.getData(3) << 8) + rx.getData(4);
          // num packets to be sent   
          num_packets = rx.getData(5) << 8 + rx.getData(6);   
           
          sendMessageToProgrammer(OK);               
        } else if (rx.getData(2) == CONTROL_PROG_DATA && in_prog) {
          packet_count++;
          
          // data starts at 16 (12 bytes xbee header + data header
          //dump_buffer(xbee.getResponse().getFrameData() + 16, "packet", xbee.getResponse().getFrameDataLength() - 16);
          // header
//        MAGIC_BYTE1, 
//        MAGIC_BYTE2, 
//        CONTROL_PROG_DATA, 
//        (address16 >> 8) & 0xff, 
//        address16 & 0xff

          int address = (rx.getData(3) << 8) + rx.getData(4); 
          
          //getDebugSerial()->print("addr msb "); getDebugSerial()->print(rx.getData(3), DEC); getDebugSerial()->print(" addr lsb "); getDebugSerial()->println(rx.getData(4), DEC);
          //getDebugSerial()->print("curaddr msb "); getDebugSerial()->print(((current_eeprom_address - EEPROM_OFFSET_ADDRESS) >> 8) & 0xff, DEC); getDebugSerial()->print(" addr lsb "); getDebugSerial()->println((current_eeprom_address - EEPROM_OFFSET_ADDRESS) & 0xff, DEC);
          
          // now write page to eeprom

          // check if the address of this packet aligns with the last write to eeprom. it could be a resend and that's ok
          if (current_eeprom_address < (address + EEPROM_OFFSET_ADDRESS)) {
            getDebugSerial()->print("WARN: expected address "); getDebugSerial()->print(current_eeprom_address, DEC); getDebugSerial()->print(" but got "); getDebugSerial()->println(address + EEPROM_OFFSET_ADDRESS, DEC);
          } else if (current_eeprom_address > (address + EEPROM_OFFSET_ADDRESS)) {
            // attempt to write beyond current eeprom address
            // this would result in a gap in data and would ultimately fail, so reject
            getDebugSerial()->print("ERROR: attempt to write @ address "); getDebugSerial()->print((address + EEPROM_OFFSET_ADDRESS) & 0xff, DEC); getDebugSerial()->print(" but current address @ "); getDebugSerial()->println(current_eeprom_address & 0xff, DEC);            
            // TODO send error reply back
            return;
          }

          // NOTE we've made it idempotent in case we get retries
          // TODO validate it's in range            
          current_eeprom_address = address + EEPROM_OFFSET_ADDRESS;

            //dump_buffer(xbee.getResponse().getFrameData() + 16, "packet", xbee.getResponse().getFrameDataLength() - 16);
            
            uint8_t len = xbee.getResponse().getFrameDataLength() - 16;
            
            if (eeprom.write(current_eeprom_address, xbee.getResponse().getFrameData() + 16, len) != 0) {
              getDebugSerial()->println("EEPROM write failure");
              return;              
            }

            current_eeprom_address+= len;
            
//          for (int i = 5; i < rx.getDataLength(); i++) {  
//            // TODO get array from xbee and write block instead for better performance  
//            if (eeprom.write(current_eeprom_address, rx.getData(i)) != 0) {
//              getDebugSerial()->println("EEPROM write failure");
//              return;
//            }
//            current_eeprom_address++;
//          }  

          if (packet_count == num_packets) {
            // should be last packet but maybe not if we got retries 
          }
          
          // TODO write a checksum in eeprom header so we can verify prior to flashing                          
          // prog data

          sendMessageToProgrammer(OK);          
        } else if (rx.getData(2) == CONTROL_FLASH_START && in_prog) {
          // done verify we got expected # packets

          getDebugSerial()->println("Flashing");
          
          // TODO verify that's what we've received            
          // NOTE redundant we have prog_size
          int psize = (rx.getData(3) << 8) + rx.getData(4);
          
          //getDebugSerial()->print("prog size "); getDebugSerial()->print(psize, DEC); getDebugSerial()->print("cur addr "); getDebugSerial()->println(current_eeprom_address - EEPROM_OFFSET_ADDRESS, DEC);
                    
          if (psize != current_eeprom_address - EEPROM_OFFSET_ADDRESS) {
              //getDebugSerial()->println("Last pckt address != prog_size");
              return;              
          } else if (psize != prog_size) {
              getDebugSerial()->println("psize != prog_size");            
              return;            
          }         

          if (flash(EEPROM_OFFSET_ADDRESS, prog_size) != 0) {
            getDebugSerial()->println("Flash failure");   
            sendMessageToProgrammer(FAILURE);            
            return; 
          } else {
            getDebugSerial()->println("Success!");    
            sendMessageToProgrammer(OK);            
          }
                            
          // reset everything
          in_prog = false;
        } else {
          // sync error, not expecting prog data   
          // TODO send error. client needs to start over
          getDebugSerial()->println("not-in-prog");
        }
        
        last_packet = millis();
      } else {
        // not a programming packet
        // TODO FORWARD
      }
      
//      if (in_prog == false) {
//        forwardPacket();
//      }
    }  
}

void setup() {
  // start usb serial on leonardo for debugging
  // usb-serial @19200 could go higher  
  Serial.begin(19200);
  
  // leonardo wait for serial
  while (!Serial);
  
  pinMode(resetPin, OUTPUT);
  
  // optiboot is 115.2K
  // uart is for programming
  Serial1.begin(115200);
  
  if (eeprom.begin(twiClock400kHz) != 0) {
    getDebugSerial()->println("eeprom failure");
    return;  
  }
  
  // we only have one Serial port (UART) so need nss for XBee
  nss.begin(9600);  
  xbee.setSerial(nss);

  getDebugSerial()->println("Ready!");
}

void loop() {   
  xbee.readPacket();

  // target arduino must be a 328 that is programmed via serial port
  
  if (xbee.getResponse().isAvailable()) {  
    // if not programming packet, relay exact bytes to the arduino. need to figure out how to get from library
    // NOTE the target sketch should ignore any programming packets it receives as that is an indication of a failed programming attempt
    handlePacket();
  } else if (xbee.getResponse().isError()) {
    getDebugSerial()->println("RX error: ");
    getDebugSerial()->println(xbee.getResponse().getErrorCode(), DEC);
  }  
  
  if (in_prog && millis() - last_packet > 5000) {
    // timeout
    getDebugSerial()->println("Prog timeout");    
    in_prog = false;
    // TODO clear eeprom
  }
  
  if (in_prog && last_packet > 0 && millis() - last_packet > XBEE_TIMEOUT) {
    prog_reset();
  }
  
  if (!in_prog) {
    // check if this has magic bytes before forwarding
    
    // pass all data (xbee packets) from remote out the xbee serial port
    // we don't need to do anything with these packets
    
    // TODO
//    while (getProgrammerSerial()->available() > 0) {
//      getXBeeSerial()->write(getProgrammerSerial()->read()); 
//    }    
  }
}
