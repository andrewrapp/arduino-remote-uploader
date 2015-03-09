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
 
#include <SoftwareSerial.h>
#include <extEEPROM.h>

// needed for uint32_t and Serial port
#include <stdint.h>
#include "HardwareSerial.h"

// finally success 2/15/15 11:38AM: Flash in 2174ms!

// Leonardo (usb-serial) is required for USBDEBUG. Due to my flagrant use of Serial.println, the Leonardo may go out of sram if VERBOSE is true and it fails in unexpected ways! :(

// NOTE: Leonardo seems to have no problem powering the xbee ~50ma and Diecimila!

// WIRING:
// unfortunately we can't use an xbee shield because we need the serial port for programming. Instead the XBee must use softserial. You can use the shield and wire the 5V,GND,TX/RX of the shield to Arduino
// Optiboot needs 115.2 so softserial is not an option
// Consider modifying optiboot to run @ 19.2 so softserial is viable (on programming, still of course need serial on target)

/*
TAKE NOTE: I2C pins vary depending on the Arduino board! (see below)

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

Programmer digital 8 -> reset
Programmer digital 11 -> XBee RX
Programmer digital 10 -> XBee TX
Programmer TX -> app arduino RX
Programmer RX -> app arduino TX

Arduino Pro
VCC -> 5V regulated
GND -> GND

NOTE: when uploading a new version of the sketcher to a Pro, remember to disconnect the serial lines from other arduino or upload will fail. 
Also you'll need to press the reset button if you don't have CTS connected (for auto-reset).
Leonardo is more flexible since upload occurs over usb-serial

TROUBLESHOOTING
- if flash_init fails with 0,0,0 response, bad news you are not talking to the bootloader, verify the resetPin is connected to Reset on the target. Also verify Serial1 (UART) wired correction and is at 115200
- check every pin connection, reset, xbee tx/rx (remember arduino tx goes to xbee rx), eeprom, power. make sure all powered devices share a com
- connection issues: check your solder joints. try different breadboard positions, try different breadboard, try different Arduinos



Ready!
*Received start packet
********************************************************
Flashing from eeprom...
Bouncing the Arduino
Talking to Optiboot
Flashed in 1571ms
*/
// ================================================================== START CONFIG ==================================================================



// ** IMPORTANT! **
// For Leonardo use Serial1 (UART) or it will try to program through usb-serial
// For atmega328 use Serial
// For megas other e.g. Serial2 should work -- UNTESTED!
HardwareSerial* progammerSerial = &Serial1;

// should we proxy serial rx/tx to softserial (xbee). if you want to use the XBee from the application arduino set to true -- if only using xbee for programming set to false
#define PROXY_SERIAL true

// The remaining config should be fine for vast majority of cases

// max time between xbee packets before timeout occurs and it kicks out of programming mode
const long PROG_TIMEOUT = 5000;

// only 115200 works with optiboot
#define OPTIBOOT_BAUD_RATE 115200
// how long to wait for a reply from optiboot before timeout (ms)
#define OPTIBOOT_READ_TIMEOUT 1000

// Currently it goes out of memory on atmega328/168 with VERBOSE true. TODO shorten strings so it doesn't go out of memory
// Must also enable a debug option (USBDEBUG or NSSDEBUG) with VERBOSE true. With atmega328/168 you may only use NSSDEBUG as the only serial port is for flashing
#define VERBOSE false
#define DEBUG_BAUD_RATE 19200
// WARNING: never set this to true for a atmega328/168 as it needs Serial(0) for programming. If you do it will certainly fail on flash()
// Only true for Leonardo (defaults to Serial(0) for debug) 
// IMPORTANT: you must have the serial monitor open when usb debug enabled or will fail after a few packets!
#define USBDEBUG true
// UNTESTED!
#define NSSDEBUG false
#define NSSDEBUG_TX 6
#define NSSDEBUG_RX 7

// NOTE: ONLY SET THIS TRUE FOR TROUBLESHOOTING. FLASHING IS NOT POSSIBLE IF SET TO TRUE
#define USE_SERIAL_FOR_DEBUG false

const int resetPin = 9;

// this can be reduced to the maximum packet size + header bytes
// memory shouldn't be an issue on the programmer since it only should ever run this sketch!
#define BUFFER_SIZE 150
#define READ_BUFFER_SIZE 150

// TODO not implemented yet
const int PROG_PAGE_RETRIES = 2;
// the address to start writing the hex to the eeprom
const int EEPROM_OFFSET_ADDRESS = 16;

// ==================================================================END CONFIG ==================================================================

// TODO define negaive error codes for sketch only. translate to byte error code and send to XBee

// we don't need magic bytes if we're not proxying but they are using only 5% of packet with nordic and much less with xbee
// any packet that has byte1 and byte 2 that equals these is a programming packet
#define MAGIC_BYTE1 0xef
#define MAGIC_BYTE2 0xac

#define START_PROGRAMMING 0xa0
#define PROGRAM_DATA 0xa1
#define STOP_PROGRAMMING 0xa2

#define CONTROL_PROG_REQUEST 0x10
#define CONTROL_PROG_DATA 0x20
#define CONTROL_FLASH_START 0x40

#define PROG_START_HEADER_SIZE 9
#define PROG_DATA_HEADER_SIZE 6
#define FLASH_START_HEADER_SIZE 6

#define VERSION = 1;

// host reply codes
// every packet must return exactly one reply: OK or ERROR. is anything > OK
// TODO make sure only one reply code is sent!
// timeout should be the only reply that is not sent immediately after rx packet received
#define OK 1
//got prog data but no start. host needs to start over
#define START_OVER 2
#define TIMEOUT 3
#define FLASH_ERROR 0x82
#define EEPROM_ERROR 0x80
#define EEPROM_WRITE_ERROR 0x81
// TODO these should be bit sets on FLASH_ERROR
#define EEPROM_READ_ERROR 0xb1
// serial lines not connected or reset pin not connected
#define NOBOOTLOADER_ERROR 0xc1
#define BOOTLOADER_REPLY_TIMEOUT 0xc2
#define BOOTLOADER_UNEXPECTED_REPLY 0xc3


// STK CONSTANTS
#define STK_OK              0x10
#define STK_INSYNC          0x14  // ' '
#define CRC_EOP             0x20  // 'SPACE'
#define STK_GET_PARAMETER   0x41  // 'A'
#define STK_ENTER_PROGMODE  0x50  // 'P'
#define STK_LEAVE_PROGMODE  0x51  // 'Q'
#define STK_LOAD_ADDRESS    0x55  // 'U'
#define STK_PROG_PAGE       0x64  // 'd'
#define STK_READ_PAGE       0x74  // 't'
#define STK_READ_SIGN       0x75  // 'u'

// =========================================================================

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

#if (NSSDEBUG) 
  SoftwareSerial nss_debug(NSSDEBUG_TX, NSSDEBUG_RX);
#endif

// TODO handle 512kb
extEEPROM eeprom(kbits_256, 1, 64);

HardwareSerial* getProgrammerSerial() {
  return progammerSerial;
}

// can only use USBDEBUG with Leonardo or other variant that supports multiple serial ports
#if (USBDEBUG)
  Stream* getDebugSerial() {
    return &Serial;  
  }
#elif (NSSDEBUG) 
  Stream* getDebugSerial() {
    return &nss_debug;  
  }
#endif

void clear_read() {
  int count = 0;
  while (getProgrammerSerial()->available() > 0) {
    getProgrammerSerial()->read();
    count++;
  }
  
  if (count > 0) {
    #if (VERBOSE && (USBDEBUG || NSSDEBUG))
      getDebugSerial()->print("clear_read: trashed "); getDebugSerial()->print(count, DEC); getDebugSerial()->println(" bytes");
    #endif
  }
}

// returns bytes read:
// returns reply length >= 0
// -2 if timeout
// -1 unexpected length
int read_optiboot_reply(uint8_t len, int timeout) {
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
  
  if (millis() - start >= timeout) {
    // timeout
    return -2; 
  }
  
  // consume any extra
  clear_read();
  
  if (pos == len) {
    return pos;
  } else {
    // TODO return error code instead of strings that take up precious memeory
    #if (USBDEBUG || NSSDEBUG)
      getDebugSerial()->print("read timeout! got "); getDebugSerial()->print(pos, DEC); getDebugSerial()->print(" byte, expected "); getDebugSerial()->print(len, DEC); getDebugSerial()->println(" bytes");
    #endif
  
    // unexpected reply length
    return -1;    
  }
}

void dump_buffer(uint8_t arr[], char context[], uint8_t len) {
  #if (USBDEBUG || NSSDEBUG)
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
  #endif
}

// send command and buffer and return length of reply
// success >= 0, otherwise error code
int send_to_optiboot(uint8_t command, uint8_t *arr, uint8_t len, uint8_t response_length) {

    #if (VERBOSE && (USBDEBUG || NSSDEBUG))
      getDebugSerial()->print("send() command: "); getDebugSerial()->println(command, HEX);
    #endif
    
    getProgrammerSerial()->write(command);

  if (arr != NULL && len > 0) {
    for (int i = 0; i < len; i++) {
      getProgrammerSerial()->write(arr[i]);   
    }
    
    #if (VERBOSE && (USBDEBUG || NSSDEBUG)) 
      dump_buffer(arr, "send->", len);
    #endif
  }
  
  getProgrammerSerial()->write(CRC_EOP);
  // make it synchronous
  getProgrammerSerial()->flush();
      
  // add 2 bytes since we always expect to get back STK_INSYNC + STK_OK
  int reply_len = read_optiboot_reply(response_length + 2, OPTIBOOT_READ_TIMEOUT);

  if (reply_len < 0) {
    if (reply_len == -2) {
      // timeout
    } else {
      // unexpected length
    }

    return -1;
  }
  
  if (VERBOSE) {
    dump_buffer(read_buffer, "send_reply", reply_len);    
  }

  if (reply_len < 2) {
    #if (USBDEBUG || NSSDEBUG) 
      getDebugSerial()->println("Invalid response");
    #endif
 
    return -1; 
  }

  if (read_buffer[0] != STK_INSYNC) {
    #if (USBDEBUG || NSSDEBUG)
      getDebugSerial()->print("No STK_INSYNC"); //getDebugSerial()->println(read_buffer[0], HEX);
    #endif

    return -1;
  }
  
  if (read_buffer[reply_len - 1] != STK_OK) {
    #if (USBDEBUG || NSSDEBUG)
      getDebugSerial()->print("Expected STK_OK but was "); getDebugSerial()->println(read_buffer[reply_len - 1], HEX);
    #endif
    
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

// returns 0 if success, < 0 on error
int flash_init() {
  clear_read();
    
    int data_len = 0;
    
    cmd_buffer[0] = 0x81;
    data_len = send_to_optiboot(STK_GET_PARAMETER, cmd_buffer, 1, 1);
    
    if (data_len == -1) {
     // seems that we're not talking to the bootloader
     //sendReply(NOBOOTLOADER_ERROR);
     return -1;   
    }
    
    #if (VERBOSE && (USBDEBUG || NSSDEBUG))
      getDebugSerial()->print("Firmware: "); getDebugSerial()->println(read_buffer[0], HEX);      
    #endif

    
    cmd_buffer[0] = 0x82;
    data_len = send_to_optiboot(STK_GET_PARAMETER, cmd_buffer, 1, 1);

    if (data_len == -1) {
      return -1;   
    }

    #if (VERBOSE && (USBDEBUG || NSSDEBUG))
      getDebugSerial()->print("Minor: "); getDebugSerial()->println(read_buffer[0], HEX);    
    #endif
    
    // this not a valid command. optiboot will send back 0x3 for anything it doesn't understand
    cmd_buffer[0] = 0x83;
    data_len = send_to_optiboot(STK_GET_PARAMETER, cmd_buffer, 1, 1);
    
    if (data_len == -1) {
      return -1;   
    } else if (read_buffer[0] != 0x3) {
      #if (USBDEBUG || NSSDEBUG)
        getDebugSerial()->print("Unxpected optiboot reply: "); getDebugSerial()->println(read_buffer[0]);
      #endif
      return -1;
    }

    data_len = send_to_optiboot(STK_READ_SIGN, NULL, 0, 3);
    
    if (data_len != 3) {      
      return -1;      
    } else if (read_buffer[0] == 0x1E && read_buffer[1] == 0x94 && read_buffer[2] == 0x6) {
      //atmega168
    } else if (read_buffer[0] == 0x1E && read_buffer[1] == 0x95 && read_buffer[2] == 0x0f) {
      //atmega328p
    } else if (read_buffer[0] == 0x1E && read_buffer[1] == 0x95 && read_buffer[2] == 0x14) {      
      //atmega328
    } else {
      #if (USBDEBUG || NSSDEBUG)
        dump_buffer(read_buffer, "Unexpected signature: ", 3);
      #endif

      return -1;
    }
    
    #if (USBDEBUG || NSSDEBUG)
      getDebugSerial()->println("Talking to Optiboot");      
    #endif
  
    // IGNORED BY OPTIBOOT
    // avrdude does a set device
    //avrdude: send: B [42] . [86] . [00] . [00] . [01] . [01] . [01] . [01] . [03] . [ff] . [ff] . [ff] . [ff] . [00] . [80] . [02] . [00] . [00] . [00] @ [40] . [00]   [20]     
    // then set device ext
    //avrdude: send: E [45] . [05] . [04] . [d7] . [c2] . [00]   [20]     
    return 0;
}

// returns 0 on success, < 0 on error
// need 3 bytes for prog_page so data should start at buf[3]
int send_page(uint8_t *addr, uint8_t *buf, uint8_t data_len) {    
    // ctrl,len,addr high/low
    // address is byte index 2,3
    
    // retry up to 2 times
    // disable retries for DEBUGging
    //for (int z = 0; z < PROG_PAGE_RETRIES + 1; z++) {
      // [55] . [00] . [00] 
      if (send_to_optiboot(STK_LOAD_ADDRESS, addr, 2, 0) == -1) {
        #if (USBDEBUG || NSSDEBUG) 
          getDebugSerial()->println("Load address failed");          
        #endif

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
      if (send_to_optiboot(STK_PROG_PAGE, buffer, data_len + 3, 0) == -1) {
        #if (USBDEBUG || NSSDEBUG) 
          getDebugSerial()->println("Prog page failed");
        #endif        
        return -1;
      }
  
      uint8_t reply_len = send_to_optiboot(STK_READ_PAGE, buffer, 3, data_len);
      
      if (reply_len == -1) {
        #if (USBDEBUG || NSSDEBUG) 
          getDebugSerial()->println("Read page failure");
        #endif          
        return -1;
      }
      
      if (reply_len != data_len) {
        #if (USBDEBUG || NSSDEBUG) 
          getDebugSerial()->println("Read page len fail");
        #endif
        return -1;
      }
      
      // TODO we can compute checksum on buffer, reset and use for the read buffer!!!!!!!!!!!!!!!
      
      #if (VERBOSE && (USBDEBUG || NSSDEBUG))
        getDebugSerial()->print("Read page length is "); getDebugSerial()->println(reply_len, DEC);      
      #endif
      
      bool verified = true;
      
      // verify each byte written matches what is returned by bootloader
      for (int i = 0; i < reply_len; i++) {        
        if (read_buffer[i] != buffer[i + 3]) {
          #if (USBDEBUG || NSSDEBUG)
            getDebugSerial()->print("Verify page fail @ "); getDebugSerial()->println(i, DEC);
          #endif
          verified = false;
          break;
        }
      }
      
      if (!verified) {
        // retry is still attempts remaining
        //if (z < PROG_PAGE_RETRIES) {
        //  getDebugSerial()->println("Failed to verify page.. retrying");
        //} else {
          #if (USBDEBUG || NSSDEBUG) 
            getDebugSerial()->println("Verify page fail");
          #endif            
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

void bounce() {    
    //clear_read();

    // Bounce the reset pin
    #if (USBDEBUG || NSSDEBUG) 
      getDebugSerial()->println("Bouncing the Arduino");
    #endif      
 
    // set reset pin low
    digitalWrite(resetPin, LOW);
    delay(200);
    digitalWrite(resetPin, HIGH);
    delay(300);
}

// returns 0 on success, < 0 on error
// blocking. takes about 1208ms for a small sketch (2KB)
int flash(int start_address, int size) {
  // now read from eeprom and program  
  long start = millis();
  
  #if (USBDEBUG || NSSDEBUG) 
    getDebugSerial()->println("Flashing from eeprom...");
  #endif

  bounce();
          
  if (flash_init() != 0) {
    #if (USBDEBUG || NSSDEBUG) 
      getDebugSerial()->println("Check failed!");
    #endif
    prog_reset();
    return -1;
  } 
    
  if (send_to_optiboot(STK_ENTER_PROGMODE, buffer, 0, 0) == -1) {
    #if (USBDEBUG || NSSDEBUG) 
      getDebugSerial()->println("STK_ENTER_PROGMODE failure");
    #endif  
    
    return -1;
  }
    
  int current_address = start_address;
  
  while (current_address < (size + EEPROM_OFFSET_ADDRESS)) {
    int len = 0;
    
    if ((size + EEPROM_OFFSET_ADDRESS) - current_address < 128) {
      len = (size + EEPROM_OFFSET_ADDRESS) - current_address;
    } else {
      len = 128;
    }
    
    #if (VERBOSE && (USBDEBUG || NSSDEBUG))
      getDebugSerial()->print("EEPROM read at address "); getDebugSerial()->print(current_address, DEC); getDebugSerial()->print(" len is "); getDebugSerial()->println(len, DEC);
    #endif
     
    // skip 2 so we leave room for the prog_page command
    int ok = eeprom.read(current_address, buffer + 3, len);
    
    if (ok != 0) {
      #if (USBDEBUG || NSSDEBUG) 
        getDebugSerial()->println("EEPROM read fail");
      #endif
      
      //sendReply(EEPROM_READ_ERROR);     
      return -1;
    }
    
    // set laod address little endian
    // gotta divide by 2
    addr[0] = ((current_address - start_address) / 2) & 0xff;
    addr[1] = (((current_address - start_address) / 2) >> 8) & 0xff;
    
    #if (VERBOSE && (USBDEBUG || NSSDEBUG))
      getDebugSerial()->print("send page len: "); getDebugSerial()->println(len, DEC);            
      dump_buffer(buffer + 3, "read from eeprom->", len);
    #endif
                  
    if (send_page(addr, buffer, len) == -1) {
      #if (USBDEBUG || NSSDEBUG) 
        getDebugSerial()->println("send page fail");
      #endif  

      return -1;
    }
    
    current_address+= len;
  }
  
  if (send_to_optiboot(STK_LEAVE_PROGMODE, buffer, 0, 0) == -1) {
    #if (USBDEBUG || NSSDEBUG) 
      getDebugSerial()->println("STK_LEAVE_PROGMODE failure");
    #endif  

    return -1;       
  }
  
  // SUCCESS!!
  #if (USBDEBUG || NSSDEBUG)
    getDebugSerial()->print("Flashed in "); getDebugSerial()->print(millis() - start, DEC); getDebugSerial()->println("ms");
  #endif
      
  return 0;
}
 
bool isProgrammingPacket(uint8_t packet[], uint8_t packet_length) {
  if (packet_length >= 4 && packet[0] == MAGIC_BYTE1 && packet[1] == MAGIC_BYTE2) {          
    return true;
  }
  
  return false;
}

bool isFlashPacket(uint8_t packet[]) {
  if (packet[2] == CONTROL_FLASH_START) {
    return true;
  }
  
  return false;
}

// not specific to the transport
// process packet and return reply code for host
int handlePacket(uint8_t packet[]) {
    // if programming start magic packet is received:
    // reset the target arduino.. determine the neecessary delay
    // send data portion of packets to serial.. but look for magic word on packet so we know it's programming data
    // NOTE: any programs that send to this radio should be shutdown or the programming would hose the arduino
    // on final packet do any verification to see if it boots

//    #if (USBDEBUG || NSSDEBUG) 
//      dump_buffer(packet, "packet", packet_length);
//    #endif
        
      // 3 bytes for head + at least one programming
      // TODO also check rx.getData(2) is one of CONTROL_PROG_X for extra measure of ensuring we are not acting on a application packet
        // echo * for each programming packet
        #if (USBDEBUG || NSSDEBUG) 
          getDebugSerial()->print("*");
        #endif
      
        if (packet[2] == CONTROL_PROG_REQUEST) {
         // start
          #if (USBDEBUG || NSSDEBUG) 
            getDebugSerial()->println("Received start packet");
          #endif  
          
          if (in_prog) {
            // TODO print warning: already in prog. reset and continue
            #if (USBDEBUG || NSSDEBUG) 
              getDebugSerial()->println("Error: in prog");
            #endif
          }

          // reset state
          prog_reset();
          
          prog_start = millis();
          in_prog = true; 
          packet_count = 0;

//				MAGIC_BYTE1, 
//				MAGIC_BYTE2, 
//				CONTROL_PROG_REQUEST, 
//				9, // length, including header
//				(sizeInBytes >> 8) & 0xff, 
//				sizeInBytes & 0xff, 
//				(numPages >> 8) & 0xff, 
//				numPages & 0xff,
//				bytesPerPage      
          // program size
          prog_size = (packet[4] << 8) + packet[5];
          // num packets to expect
          num_packets = packet[6] << 8 + packet[7];   
        } else if (packet[2] == CONTROL_PROG_DATA && in_prog) {
          packet_count++;
          
          // data starts at 16 (12 bytes xbee header + data header
          //dump_buffer(xbee.getResponse().getFrameData() + 16, "packet", xbee.getResponse().getFrameDataLength() - 16);
          // header
//				MAGIC_BYTE1, 
//				MAGIC_BYTE2, 
//				CONTROL_PROG_DATA, 
//				packetLength + 6, //length + 6 bytes for header
//				(address >> 8) & 0xff, 
//				address & 0xff
          int packet_len = packet[3];
          int address = (packet[4] << 8) + packet[5]; 
          
          //getDebugSerial()->print("addr msb "); getDebugSerial()->print(rx.getData(3), DEC); getDebugSerial()->print(" addr lsb "); getDebugSerial()->println(rx.getData(4), DEC);
          //getDebugSerial()->print("curaddr msb "); getDebugSerial()->print(((current_eeprom_address - EEPROM_OFFSET_ADDRESS) >> 8) & 0xff, DEC); getDebugSerial()->print(" addr lsb "); getDebugSerial()->println((current_eeprom_address - EEPROM_OFFSET_ADDRESS) & 0xff, DEC);
          
          // now write page to eeprom

          // check if the address of this packet aligns with the last write to eeprom. it could be a resend and that's ok
          if (current_eeprom_address < (address + EEPROM_OFFSET_ADDRESS)) {
            #if (USBDEBUG || NSSDEBUG)
              getDebugSerial()->print("WARN: expected address "); getDebugSerial()->print(current_eeprom_address, DEC); getDebugSerial()->print(" but got "); getDebugSerial()->println(address + EEPROM_OFFSET_ADDRESS, DEC);
            #endif
          } else if (current_eeprom_address > (address + EEPROM_OFFSET_ADDRESS)) {
            // attempt to write beyond current eeprom address
            // this would result in a gap in data and would ultimately fail, so reject
            #if (USBDEBUG || NSSDEBUG)
              getDebugSerial()->print("ERROR: attempt to write @ address "); getDebugSerial()->print((address + EEPROM_OFFSET_ADDRESS) & 0xff, DEC); getDebugSerial()->print(" but current address @ "); getDebugSerial()->println(current_eeprom_address & 0xff, DEC);            
            #endif
            
            return START_OVER;
          }

          // NOTE we've made it idempotent in case we get retries
          // TODO validate it's in range            
          current_eeprom_address = address + EEPROM_OFFSET_ADDRESS;

//          #if (USBDEBUG || NSSDEBUG)
//            getDebugSerial()->print("curaddr "); getDebugSerial()->println(current_eeprom_address, DEC); 
//          #endif

          //dump_buffer(packet + 5, "packet", packet_length - 5);
            
          uint8_t data_len = packet_len - PROG_DATA_HEADER_SIZE;
            
          if (eeprom.write(current_eeprom_address, packet + PROG_DATA_HEADER_SIZE, data_len) != 0) {
            #if (USBDEBUG || NSSDEBUG) 
              getDebugSerial()->println("EEPROM write failure");
            #endif  
            
            return EEPROM_WRITE_ERROR;
          }
          
          current_eeprom_address+= data_len;

//          #if (USBDEBUG || NSSDEBUG)
//            getDebugSerial()->print("len is "); getDebugSerial()->println(len, DEC);
//            getDebugSerial()->print("addr+len "); getDebugSerial()->println(current_eeprom_address, DEC); 
//          #endif
          
          if (packet_count == num_packets) {
            // should be last packet but maybe not if we got retries 
          }
          
          // TODO write a checksum in eeprom header so we can verify prior to flashing                          
          // prog data
        } else if (isFlashPacket(packet) && in_prog) {
          // done verify we got expected # packets

//				MAGIC_BYTE1, 
//				MAGIC_BYTE2, 
//				CONTROL_START_FLASH, 
//				packetLength + 6, //length + 6 bytes for header
//				(progSize >> 8) & 0xff, 
//				progSize & 0xff

          #if (USBDEBUG || NSSDEBUG) 
            getDebugSerial()->println("");
          #endif  

        // debug remove
        #if (USBDEBUG || NSSDEBUG) 
          getDebugSerial()->println("Flash start packet");
        #endif
        
          // TODO verify that's what we've received            
          // NOTE redundant we have prog_size
          int psize = (packet[4] << 8) + packet[5];
                    
          if (psize != current_eeprom_address - EEPROM_OFFSET_ADDRESS) {
            #if (USBDEBUG || NSSDEBUG) 
              getDebugSerial()->print("psize "); getDebugSerial()->print(psize, HEX); getDebugSerial()->print(",cur addr "); getDebugSerial()->println(current_eeprom_address - EEPROM_OFFSET_ADDRESS, HEX);
            #endif              
            // TODO make codes more explicit
            return START_OVER;             
          } else if (psize != prog_size) {
            #if (USBDEBUG || NSSDEBUG) 
              getDebugSerial()->println("psize != prog_size");
            #endif              
            
            return START_OVER;             
          }         

          // set to optiboot speed
          getProgrammerSerial()->begin(OPTIBOOT_BAUD_RATE);
  
          if (flash(EEPROM_OFFSET_ADDRESS, prog_size) != 0) {
            #if (USBDEBUG || NSSDEBUG)
              getDebugSerial()->println("Flash failure");
            #endif              
            return FLASH_ERROR;
          }
                    
          // reset everything
          prog_reset();
        } else {
          // sync error, not expecting prog data   
          // TODO send error. client needs to start over
          #if (USBDEBUG || NSSDEBUG) 
            getDebugSerial()->println("not-in-prog");
          #endif  
          
          return START_OVER;
        }
        
        // update so we know when to timeout
        last_packet = millis();
        return OK;
}

int setup_core() {
   // leonardo wait for serial
  #if (USBDEBUG)
    // start usb serial on leonardo for DEBUGging
    // usb-serial @19200 could go higher  
    // TODO use getDebugSerial or variable
    Serial.begin(DEBUG_BAUD_RATE);    
    // only necessary for leonardo. safe for others
    while (!Serial);
  #elif (NSSDEBUG) 
    nss_debug.begin(DEBUG_BAUD_RATE);
  #endif
  
  pinMode(resetPin, OUTPUT);
  
  // optiboot is 115.2K
  // Start with 9600 for xbee
  // then switch to 115.2 for flashing for Optiboot
  // this is a terrible idea, only use as final act of desparation!
  #if (!USE_SERIAL_FOR_DEBUG)
    getProgrammerSerial()->begin(DEBUG_BAUD_RATE);
  #endif

  
  if (eeprom.begin(twiClock400kHz) != 0) {
    #if (USBDEBUG || NSSDEBUG) 
      getDebugSerial()->println("eeprom failure");
    #endif  
    
    return EEPROM_ERROR;
  } 
  
  return 0;
}

