#include <Wire.h>
#include <extEEPROM.h>
#include <SoftwareSerial.h>
#include <XBee.h>

// finally success 2/15/15 11:38AM: Flash in 2174ms!

// Leonardo (usb-serial) is required for debug. Alternatively it could be adpated to use softserial
// due to my flagrant use of Serial.println, the leonardo will go out of sram if version is true :(
#define VERBOSE false
// the Serial Monitor must be open when DEBUG true or programming will fail!
// print debug to debug serial
#define DEBUG false
// TODO
#define USBDEBUG

// should we proxy serial rx/tx to softserial (xbee) 
#define PROXY_SERIAL true

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

// WIRING:
// unfortunately we can't use an xbee shield because we need the serial port for programming. Instead the XBee must use softserial. You can use the shield and wire the 5V,GND,TX/RX of the shield to Arduino
// Optiboot needs 115.2 so softserial is not an option
// Consider modifying optiboot to run @ 19.2 so softserial is viable (on programming, still of course need serial on target)

// TROUBLESHOOTING. 
// if flash_init fails with 0,0,0 response, bad news you are not talking to the bootloader, verify the resetPin is connected to Reset on the target. Also verify Serial1 (UART) wired correction and is at 115200

// NOTE: Leonardo seems to have no problem powering the xbee ~50ma and Diecimila!
// NOTE: Weird things can happen if you have too many debug/println statements as each string literal consumes memory. If the sketch runs out of memeory of course it doesn't function and in some cases it also inhibits Leonardo from uploading sketches
// Keep your print statements short and concise. If you can't upload, power on leonardo and upload a blank sketch and that should fix it.

const int softTxPin = 11;
const int softRxPin = 12;
const int resetPin = 8;

const int PROG_PAGE_RETRIES = 2;
const int EEPROM_OFFSET_ADDRESS = 16;
// max time between optiboot commands before we send a noop.. not so relevant when using eeprom
//const int MAX_OPTI_DELAY = 300;
// if we don't receive a packet every X ms, timeout
const long XBEE_TIMEOUT = 5000;

XBee xbee = XBee();
XBeeResponse response = XBeeResponse();
ZBRxResponse rx = ZBRxResponse();

uint8_t xbeeTxPayload[] = { 0 };
uint32_t COORD_MSB_ADDRESS = 0x0013a200;
uint32_t COORD_LSB_ADDRESS = 0x408b98fe;

// Coordinator/XMPP Gateway
XBeeAddress64 addr64 = XBeeAddress64(COORD_MSB_ADDRESS, COORD_LSB_ADDRESS);
ZBTxRequest tx = ZBTxRequest(addr64, xbeeTxPayload, sizeof(xbeeTxPayload));
ZBTxStatusResponse txStatus = ZBTxStatusResponse();

uint8_t cmd_buffer[1];
uint8_t addr[2];
uint8_t buffer[BUFFER_SIZE];
uint8_t read_buffer[READ_BUFFER_SIZE];

int packet_count = 0;
int num_packets = 0;
int prog_size = 0;
bool in_prog = false;
long prog_start = 0;
long last_packet = 0;
int current_eeprom_address = EEPROM_OFFSET_ADDRESS;

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

#ifdef USBDEBUG
  Stream* getDebugSerial() {
    return &Serial;  
  }
#endif

void clear_read() {
  int count = 0;
  while (getProgrammerSerial()->available() > 0) {
    getProgrammerSerial()->read();
    count++;
  }
  
  if (count > 0) {
    if (VERBOSE && DEBUG) {
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
  if (DEBUG) {
    getDebugSerial()->print("read timeout! got "); getDebugSerial()->print(pos, DEC); getDebugSerial()->print(" byte, expected "); getDebugSerial()->print(len, DEC); getDebugSerial()->println(" bytes");
  }
  return -1;
}

void dump_buffer(uint8_t arr[], char context[], uint8_t len) {
  if (DEBUG) {
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
}

// Send command and buffer and return length of reply
int send(uint8_t command, uint8_t *arr, uint8_t len, uint8_t response_length) {

    if (VERBOSE && DEBUG) {
      getDebugSerial()->print("send() unexpected command: "); getDebugSerial()->println(command, HEX);
    }
    
    getProgrammerSerial()->write(command);

  if (arr != NULL && len > 0) {
    for (int i = 0; i < len; i++) {
      getProgrammerSerial()->write(arr[i]);   
    }
    
    if (VERBOSE && DEBUG) dump_buffer(arr, "send()->", len);
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
    if (DEBUG) getDebugSerial()->println("Invalid response");
    return -1; 
  }

  if (read_buffer[0] != STK_INSYNC) {
    if (DEBUG) {
      getDebugSerial()->print("Expected STK_INSYNC but was "); getDebugSerial()->println(read_buffer[0], HEX);
    }
    
    return -1;
  }
  
  if (read_buffer[reply_len - 1] != STK_OK) {
    if (DEBUG) {
      getDebugSerial()->print("Expected STK_OK but was "); getDebugSerial()->println(read_buffer[reply_len - 1], HEX);
    }
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
//  update_last_command();
  
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
    
    if (VERBOSE && DEBUG) {
      getDebugSerial()->print("Firmware: "); getDebugSerial()->println(read_buffer[0], HEX);      
    }

    
    cmd_buffer[0] = 0x82;
    data_len = send(STK_GET_PARAMETER, cmd_buffer, 1, 1);

    if (data_len == -1) {
     return -1;   
    }

    if (VERBOSE && DEBUG) {
      getDebugSerial()->print("Minor: "); getDebugSerial()->println(read_buffer[0], HEX);    
    }
    
    // this not a valid command. optiboot will send back 0x3 for anything it doesn't understand
    cmd_buffer[0] = 0x83;
    data_len = send(STK_GET_PARAMETER, cmd_buffer, 1, 1);
    
    if (data_len == -1) {
      return -1;   
    } else if (read_buffer[0] != 0x3) {
      if (DEBUG) {
        getDebugSerial()->print("Unxpected optiboot reply: "); getDebugSerial()->println(read_buffer[0]);
      }
      return -1;
    }

    data_len = send(STK_READ_SIGN, NULL, 0, 3);
    
    if (data_len != 3) {      
      return -1;      
    } else if (read_buffer[0] != 0x1E && read_buffer[1] != 0x94 && read_buffer[2] != 0x6) {
      if (DEBUG) getDebugSerial()->println("Signature invalid");
      return -1;
    }
    
    if (DEBUG) getDebugSerial()->println("Talking to Optiboot");      
  
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
        if (DEBUG) getDebugSerial()->println("Load address failed");          

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
        if (DEBUG) getDebugSerial()->println("Prog page failed");
        return -1;
      }
  
      uint8_t reply_len = send(STK_READ_PAGE, buffer, 3, data_len);
      
      if (reply_len == -1) {
        if (DEBUG) getDebugSerial()->println("Read page failure");
        return -1;
      }
      
      if (reply_len != data_len) {
        if (DEBUG) getDebugSerial()->println("Read page len fail");
        return -1;
      }
      
      // TODO we can compute checksum on buffer, reset and use for the read buffer!!!!!!!!!!!!!!!
      
      if (VERBOSE && DEBUG) {
        getDebugSerial()->print("Read page length is "); getDebugSerial()->println(reply_len, DEC);      
      }
      
      bool verified = true;
      
      // verify each byte written matches what is returned by bootloader
      for (int i = 0; i < reply_len; i++) {        
        if (read_buffer[i] != buffer[i + 3]) {
          if (DEBUG) {
            getDebugSerial()->print("Verify page fail @ "); getDebugSerial()->println(i, DEC);
          }
          verified = false;
          break;
        }
      }
      
      if (!verified) {
        // retry is still attempts remaining
        //if (z < PROG_PAGE_RETRIES) {
        //  getDebugSerial()->println("Failed to verify page.. retrying");
        //} else {
          if (DEBUG) getDebugSerial()->println("Verify page fail");
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
  packet_count = 0;
  num_packets = 0;
  prog_size = 0;
  in_prog = false;
  prog_start = 0;
  last_packet = 0;
  current_eeprom_address = EEPROM_OFFSET_ADDRESS;
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

void forwardPacket() {
  // not programming packet, so proxy all xbee traffic to Arduino
  // prob cleaner way to do this if I think about it some more
  
  if (DEBUG) getDebugSerial()->println("Forwarding packet");    
        
  // send start byte, length, api, then frame data + checksum
  sendByte(START_BYTE, false);
  sendByte(xbee.getResponse().getMsbLength(), true);
  sendByte(xbee.getResponse().getLsbLength(), true);        
  sendByte(xbee.getResponse().getApiId(), true);

  uint8_t* frameData = xbee.getResponse().getFrameData();
   
  for (int i = 0; i < xbee.getResponse().getFrameDataLength(); i++) {
    sendByte(*(frameData + i), true);
  }
   
   sendByte(xbee.getResponse().getChecksum(), true);  
}

// blocking. takes about 1208ms for a small sketch (2KB)
int flash(int start_address, int size) {
  // now read from eeprom and program  
  long start = millis();
  
  bounce();
  
  if (flash_init() != 0) {
    if (DEBUG) getDebugSerial()->println("Check failed!");
    prog_reset();
    return -1;
  } 
  
  if (send(STK_ENTER_PROGMODE, buffer, 0, 0) == -1) {
    if (DEBUG) getDebugSerial()->println("STK_ENTER_PROGMODE failure");
    return -1;
  }    
  
  if (DEBUG) getDebugSerial()->println("Flashing from eeprom...");
    
  int current_address = start_address;
  
  while (current_address < (size + EEPROM_OFFSET_ADDRESS)) {
    int len = 0;
    
    if ((size + EEPROM_OFFSET_ADDRESS) - current_address < 128) {
      len = (size + EEPROM_OFFSET_ADDRESS) - current_address;
    } else {
      len = 128;
    }
    
    if (VERBOSE && DEBUG) {
      getDebugSerial()->print("EEPROM read at address "); getDebugSerial()->print(current_address, DEC); getDebugSerial()->print(" len is "); getDebugSerial()->println(len, DEC);
    }
     
    // skip 2 so we leave room for the prog_page command
    int ok = eeprom.read(current_address, buffer + 3, len);
    
    if (ok != 0) {
      if (DEBUG) getDebugSerial()->println("EEPROM read fail");
      return -1;
    }
    
    // set laod address little endian
    // gotta divide by 2
    addr[0] = ((current_address - start_address) / 2) & 0xff;
    addr[1] = (((current_address - start_address) / 2) >> 8) & 0xff;
    
    if (VERBOSE && DEBUG) {
      getDebugSerial()->print("Sending page len: "); getDebugSerial()->println(len, DEC);            
      dump_buffer(buffer + 3, "read from eeprom->", len);
    }
                  
    if (send_page(addr, buffer, len) == -1) {
      if (DEBUG) getDebugSerial()->println("Send page fail");
      return -1;
    }
    
    current_address+= len;
  }
  
  if (send(STK_LEAVE_PROGMODE, buffer, 0, 0) == -1) {
    if (DEBUG) getDebugSerial()->println("STK_LEAVE_PROGMODE failure");
    return -1;       
  }
  
  // SUCCESS!!
  if (DEBUG) {
    getDebugSerial()->print("Flashed in "); getDebugSerial()->print(millis() - start, DEC); getDebugSerial()->println("ms");
  }
      
  return 0;
}

void bounce() {    
    //clear_read();

    // Bounce the reset pin
    if (DEBUG) getDebugSerial()->println("Bouncing the Arduino");
 
    // set reset pin low
    digitalWrite(resetPin, LOW);
    delay(200);
    digitalWrite(resetPin, HIGH);
    delay(300);
}

int sendMessageToProgrammer(uint8_t status) {
  xbeeTxPayload[0] = status;
  // TODO send with magic packet host can differentiate between relayed packets and programming ACKS
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
        if (DEBUG) getDebugSerial()->println("TX fail");
      }
    }      
  } else if (xbee.getResponse().isError()) {
    if (DEBUG) {    
      getDebugSerial()->print("TX error:");  
      getDebugSerial()->print(xbee.getResponse().getErrorCode());
    }
  } else {
    if (DEBUG) getDebugSerial()->println("TX timeout");
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
      // now fill our zb rx class
      xbee.getResponse().getZBRxResponse(rx);
      
      // 3 bytes for head + at least one programming
      if (rx.getDataLength() >= 4 && rx.getData(0) == MAGIC_BYTE1 && rx.getData(1) == MAGIC_BYTE2) {          
        // echo * for each programming packet
        if (DEBUG) {        
          getDebugSerial()->print("*");
        }
      
        if (rx.getData(2) == CONTROL_PROG_REQUEST) {
         // start
          if (DEBUG) getDebugSerial()->println("Received start xbee packet");
          
          if (in_prog) {
            if (DEBUG) getDebugSerial()->println("Error: in prog");
            // TODO send error to client
            return;
          }

          // reset state
          prog_reset();
                    
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
            if (DEBUG) {
              getDebugSerial()->print("WARN: expected address "); getDebugSerial()->print(current_eeprom_address, DEC); getDebugSerial()->print(" but got "); getDebugSerial()->println(address + EEPROM_OFFSET_ADDRESS, DEC);
            }
          } else if (current_eeprom_address > (address + EEPROM_OFFSET_ADDRESS)) {
            // attempt to write beyond current eeprom address
            // this would result in a gap in data and would ultimately fail, so reject
            if (DEBUG) {
              getDebugSerial()->print("ERROR: attempt to write @ address "); getDebugSerial()->print((address + EEPROM_OFFSET_ADDRESS) & 0xff, DEC); getDebugSerial()->print(" but current address @ "); getDebugSerial()->println(current_eeprom_address & 0xff, DEC);            
            }
            // TODO send error reply back
            return;
          }

          // NOTE we've made it idempotent in case we get retries
          // TODO validate it's in range            
          current_eeprom_address = address + EEPROM_OFFSET_ADDRESS;

            //dump_buffer(xbee.getResponse().getFrameData() + 16, "packet", xbee.getResponse().getFrameDataLength() - 16);
            
            uint8_t len = xbee.getResponse().getFrameDataLength() - 16;
            
            if (eeprom.write(current_eeprom_address, xbee.getResponse().getFrameData() + 16, len) != 0) {
              if (DEBUG) getDebugSerial()->println("EEPROM write failure");
              return;              
            }

            current_eeprom_address+= len;
          if (packet_count == num_packets) {
            // should be last packet but maybe not if we got retries 
          }
          
          // TODO write a checksum in eeprom header so we can verify prior to flashing                          
          // prog data

          sendMessageToProgrammer(OK);          
        } else if (rx.getData(2) == CONTROL_FLASH_START && in_prog) {
          // done verify we got expected # packets

          if (DEBUG) getDebugSerial()->println("");
          
          // TODO verify that's what we've received            
          // NOTE redundant we have prog_size
          int psize = (rx.getData(3) << 8) + rx.getData(4);
                    
          if (psize != current_eeprom_address - EEPROM_OFFSET_ADDRESS) {
              return;              
          } else if (psize != prog_size) {
            if (DEBUG) getDebugSerial()->println("psize != prog_size");
            return;            
          }         

          // make it fast for optiboot
          Serial1.begin(115200);
          
          if (flash(EEPROM_OFFSET_ADDRESS, prog_size) != 0) {
            if (DEBUG) getDebugSerial()->println("Flash failure");
            sendMessageToProgrammer(FAILURE);            
          } else {
            sendMessageToProgrammer(OK);            
          }
          
          // resume xbee speed
          Serial1.begin(9600);

          // reset everything
          prog_reset();
        } else {
          // sync error, not expecting prog data   
          // TODO send error. client needs to start over
          if (DEBUG) getDebugSerial()->println("not-in-prog");
        }
        
        last_packet = millis();
      } else {
        // not a programming packet. forward along
        if (PROXY_SERIAL) {
          forwardPacket();                  
        }
      }
    }  
}

void setup() {  
  // leonardo wait for serial
  if (DEBUG) {
    // start usb serial on leonardo for debugging
    // usb-serial @19200 could go higher  
    Serial.begin(19200);    
    while (!Serial);
  }
  
  pinMode(resetPin, OUTPUT);
  
  // optiboot is 115.2K
  // uart is for programming
  // 9600 for xbee
  // 115.2 for flashing, set in the flash function
  Serial1.begin(9600);
  
  if (eeprom.begin(twiClock400kHz) != 0) {
    if (DEBUG) getDebugSerial()->println("eeprom failure");
    return;  
  }
  
  // we only have one Serial port (UART) so need nss for XBee
  nss.begin(9600);  
  xbee.setSerial(nss);

  if (DEBUG) getDebugSerial()->println("Ready!");
}

void loop() {  
  xbee.readPacket();

  // target arduino must be a 328 that is programmed via serial port
  
  if (xbee.getResponse().isAvailable()) {  
    // if not programming packet, relay exact bytes to the arduino. need to figure out how to get from library
    // NOTE the target sketch should ignore any programming packets it receives as that is an indication of a failed programming attempt
    handlePacket();
  } else if (xbee.getResponse().isError()) {
    if (DEBUG) getDebugSerial()->println("RX error: ");
    if (DEBUG) getDebugSerial()->println(xbee.getResponse().getErrorCode(), DEC);
  }  
  
  if (in_prog && last_packet > 0 && (millis() - last_packet) > XBEE_TIMEOUT) {
    // timeout
    if (DEBUG) getDebugSerial()->println("Prog timeout");
    prog_reset();
  }
  
  // don't need to test in_prog. if in prog we are just collecting packets so can keep relaying. when flashing, it's blocking so will never get here
  // forward packets from target out the radio
  if (PROXY_SERIAL) {
    while (getProgrammerSerial()->available() > 0) {
      int b = getProgrammerSerial()->read();
      getXBeeSerial()->write(b); 
    }      
  }
}
