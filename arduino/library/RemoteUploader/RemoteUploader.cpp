/**
 * Copyright (c) 2015 Andrew Rapp. All rights reserved.
 *
 * This file is part of arduino-remote-uploader
 *
 * arduino-remote-uploader is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * arduino-remote-uploader is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with arduino-remote-uploader.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <RemoteUploader.h>

#if defined(ARDUINO) && ARDUINO >= 100
  #include "Arduino.h"
#else
  #include "WProgram.h"
#endif

#include "HardwareSerial.h"

// Leonardo (usb-serial) is required for DEBUG. Due to my flagrant use of Serial.println, the Leonardo may go out of sram if VERBOSE is true and it fails in unexpected ways! :(
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

NOTE: when uploading a new version of this firmware to a Pro, remember to disconnect the serial lines from other arduino or upload will fail. 
Also you'll need to press the reset button if you don't have CTS connected (for auto-reset).
Leonardo is more flexible since upload occurs over usb-serial

TROUBLESHOOTING
- if flashInit fails with 0,0,0 response, bad news you are not talking to the bootloader, verify the resetPin is connected to Reset on the target. Also verify Serial1 (UART) wired correction and is at 115200
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

// =========================================================================



// TODO review error codes and handling


RemoteUploader::RemoteUploader() {
  // defaults//zero state
  uint8_t resetPin = 9;
  int packetCount = 0;
  int numPackets = 0;
  int programSize = 0;
  bool inProgramming = false;
  long programmingStartMillis = 0;
  long lastUpdateAtMillis = 0;
  int currentEEPROMAddress = EEPROM_OFFSET_ADDRESS;
}

bool RemoteUploader::inProgrammingMode() {
  return inProgramming;
}

long RemoteUploader::getLastPacketMillis() {
  return lastUpdateAtMillis;
}

HardwareSerial* RemoteUploader::getProgrammerSerial() {
  return progammerSerial;
}

#if (DEBUG)
  Stream* RemoteUploader::getDebugSerial() {
    return debugSerial;
  }

  void RemoteUploader::setDebugSerial(Stream* _debugSerial) {
    debugSerial = _debugSerial;
  }
#endif


void RemoteUploader::clearRead() {
  int count = 0;
  while (getProgrammerSerial()->available() > 0) {
    getProgrammerSerial()->read();
    count++;
  }
  
  if (count > 0) {
    #if (VERBOSE && DEBUG)
      getDebugSerial()->print("clearRead: trashed "); getDebugSerial()->print(count, DEC); getDebugSerial()->println(" bytes");
    #endif
  }
}

// returns bytes read:
// returns reply length >= 0
// -2 if timeout
// -1 unexpected length
int RemoteUploader::readOptibootReply(uint8_t len, int timeout) {
  long start = millis();
  int pos = 0;

  while (millis() - start < timeout) {          
    if (getProgrammerSerial()->available() > 0) {
      readBuffer[pos] = getProgrammerSerial()->read();
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
  clearRead();
  
  if (pos == len) {
    return pos;
  } else {
    // TODO return error code instead of strings that take up precious memeory
    #if (DEBUG)
      getDebugSerial()->print("read timeout! got "); getDebugSerial()->print(pos, DEC); getDebugSerial()->print(" byte, expected "); getDebugSerial()->print(len, DEC); getDebugSerial()->println(" bytes");
    #endif
  
    // unexpected reply length
    return -1;    
  }
}

void RemoteUploader::dumpBuffer(uint8_t arr[], char context[], uint8_t len) {
  #if (DEBUG)
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
int RemoteUploader::sendToOptiboot(uint8_t command, uint8_t *arr, uint8_t len, uint8_t responseLength) {

    #if (VERBOSE && DEBUG)
      getDebugSerial()->print("send() command: "); getDebugSerial()->println(command, HEX);
    #endif
    
    getProgrammerSerial()->write(command);

  if (arr != NULL && len > 0) {
    for (int i = 0; i < len; i++) {
      getProgrammerSerial()->write(arr[i]);   
    }
    
    #if (VERBOSE && DEBUG) 
      dumpBuffer(arr, "send->", len);
    #endif
  }
  
  getProgrammerSerial()->write(CRC_EOP);
  // make it synchronous
  getProgrammerSerial()->flush();
      
  // add 2 bytes since we always expect to get back STK_INSYNC + STK_OK
  int replyLen = readOptibootReply(responseLength + 2, OPTIBOOT_READ_TIMEOUT);

  if (replyLen < 0) {
    if (replyLen == -2) {
      // timeout
    } else {
      // unexpected length
    }

    return -1;
  }
  
  if (VERBOSE) {
    dumpBuffer(readBuffer, "send-reply", replyLen);    
  }

  if (replyLen < 2) {
    #if (DEBUG) 
      getDebugSerial()->println("Invalid response");
    #endif
 
    return -1; 
  }

  if (readBuffer[0] != STK_INSYNC) {
    #if (DEBUG)
      getDebugSerial()->print("No STK_INSYNC"); //getDebugSerial()->println(readBuffer[0], HEX);
    #endif

    return -1;
  }
  
  if (readBuffer[replyLen - 1] != STK_OK) {
    #if (DEBUG)
      getDebugSerial()->print("Expected STK_OK but was "); getDebugSerial()->println(readBuffer[replyLen - 1], HEX);
    #endif
    
    return -1;    
  }
  
  // rewrite buffer without the STK_INSYNC and STK_OK
  uint8_t dataReply = replyLen - 2;
  
  for (int i = 0; i < dataReply; i++) {
    readBuffer[i] = readBuffer[i+1];
  }
  
  // zero the ok
  readBuffer[replyLen - 1] = 0;
  
  // return the data portion of the length
  return dataReply;
}

// returns 0 if success, < 0 on error
int RemoteUploader::flashInit() {
  clearRead();
    
    int dataLen = 0;
    
    cmd_buffer[0] = 0x81;
    dataLen = sendToOptiboot(STK_GET_PARAMETER, cmd_buffer, 1, 1);
    
    if (dataLen == -1) {
     // seems that we're not talking to the bootloader
     // TODO NOBOOTLOADER_ERROR
     return -1;   
    }
    
    #if (VERBOSE && DEBUG)
      getDebugSerial()->print("Firmware: "); getDebugSerial()->println(readBuffer[0], HEX);      
    #endif

    
    cmd_buffer[0] = 0x82;
    dataLen = sendToOptiboot(STK_GET_PARAMETER, cmd_buffer, 1, 1);

    if (dataLen == -1) {
      return -1;   
    }

    #if (VERBOSE && DEBUG)
      getDebugSerial()->print("Minor: "); getDebugSerial()->println(readBuffer[0], HEX);    
    #endif
    
    // this not a valid command. optiboot will send back 0x3 for anything it doesn't understand
    cmd_buffer[0] = 0x83;
    dataLen = sendToOptiboot(STK_GET_PARAMETER, cmd_buffer, 1, 1);
    
    if (dataLen == -1) {
      return -1;   
    } else if (readBuffer[0] != 0x3) {
      #if (DEBUG)
        getDebugSerial()->print("Unxpected optiboot reply: "); getDebugSerial()->println(readBuffer[0]);
      #endif
      return -1;
    }

    dataLen = sendToOptiboot(STK_READ_SIGN, NULL, 0, 3);
    
    if (dataLen != 3) {      
      return -1;      
    } else if (readBuffer[0] == 0x1E && readBuffer[1] == 0x94 && readBuffer[2] == 0x6) {
      //atmega168
    } else if (readBuffer[0] == 0x1E && readBuffer[1] == 0x95 && readBuffer[2] == 0x0f) {
      //atmega328p
    } else if (readBuffer[0] == 0x1E && readBuffer[1] == 0x95 && readBuffer[2] == 0x14) {      
      //atmega328
    } else {
      #if (DEBUG)
        dumpBuffer(readBuffer, "Unexpected signature: ", 3);
      #endif

      return -1;
    }
    
    #if (DEBUG)
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
int RemoteUploader::sendPageToOptiboot(uint8_t *addr, uint8_t *buf, uint8_t dataLen) {    
    // ctrl,len,addr high/low
    // address is byte index 2,3
    
    // retry up to 2 times
    // disable retries for debugging
    //for (int z = 0; z < PROG_PAGE_RETRIES + 1; z++) {
      // [55] . [00] . [00] 
      if (sendToOptiboot(STK_LOAD_ADDRESS, addr, 2, 0) == -1) {
        #if (DEBUG) 
          getDebugSerial()->println("Load address failed");          
        #endif

        return -1;
      }
       
      // [64] . [00] . [80] F [46] . [0c] .
      
      // rewrite buffer to make things easier
      // data starts at addr_offset + 2
      // format of prog_page is 0x00, dataLen, 0x46, [data]
      buffer[0] = 0;
      buffer[1] = dataLen;
      buffer[2] = 0x46;
      //remaining buffer is data
      
      // add 3 to dataLen
      if (sendToOptiboot(STK_PROG_PAGE, buffer, dataLen + 3, 0) == -1) {
        #if (DEBUG) 
          getDebugSerial()->println("Prog page failed");
        #endif        
        return -1;
      }
  
      uint8_t replyLen = sendToOptiboot(STK_READ_PAGE, buffer, 3, dataLen);
      
      if (replyLen == -1) {
        #if (DEBUG) 
          getDebugSerial()->println("Read page failure");
        #endif          
        return -1;
      }
      
      if (replyLen != dataLen) {
        #if (DEBUG) 
          getDebugSerial()->println("Read page len fail");
        #endif
        return -1;
      }
      
      // TODO we can compute checksum on buffer, reset and use for the read buffer!!!!!!!!!!!!!!!
      
      #if (VERBOSE && DEBUG)
        getDebugSerial()->print("Read page length is "); getDebugSerial()->println(replyLen, DEC);      
      #endif
      
      bool verified = true;
      
      // verify each byte written matches what is returned by bootloader
      for (int i = 0; i < replyLen; i++) {        
        if (readBuffer[i] != buffer[i + 3]) {
          #if (DEBUG)
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
          #if (DEBUG) 
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
void RemoteUploader::reset() {
  packetCount = 0;
  numPackets = 0;
  programSize = 0;
  inProgramming = false;
  programmingStartMillis = 0;
  lastUpdateAtMillis = 0;
  currentEEPROMAddress = EEPROM_OFFSET_ADDRESS;
}

void RemoteUploader::bounce() {    
    //clearRead();

    // Bounce the reset pin
    #if (DEBUG) 
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
int RemoteUploader::flash(int startAddress, int size) {
  // now read from eeprom and program  
  long start = millis();
  
  #if (DEBUG) 
    getDebugSerial()->println("Flashing from eeprom...");
  #endif

  bounce();
          
  if (flashInit() != 0) {
    #if (DEBUG) 
      getDebugSerial()->println("Check failed!");
    #endif
    reset();
    return -1;
  } 
    
  if (sendToOptiboot(STK_ENTER_PROGMODE, buffer, 0, 0) == -1) {
    #if (DEBUG) 
      getDebugSerial()->println("STK_ENTER_PROGMODE failure");
    #endif  
    
    return -1;
  }
    
  int currentAddress = startAddress;
  
  while (currentAddress < (size + EEPROM_OFFSET_ADDRESS)) {
    int len = 0;
    
    if ((size + EEPROM_OFFSET_ADDRESS) - currentAddress < PROG_PAGE_SIZE) {
      len = (size + EEPROM_OFFSET_ADDRESS) - currentAddress;
    } else {
      len = PROG_PAGE_SIZE;
    }
    
    #if (VERBOSE && DEBUG)
      getDebugSerial()->print("EEPROM read at address "); getDebugSerial()->print(currentAddress, DEC); getDebugSerial()->print(" len is "); getDebugSerial()->println(len, DEC);
    #endif
     
    // skip 2 so we leave room for the prog_page command
    int ok = eeprom->read(currentAddress, buffer + 3, len);
    
    if (ok != 0) {
      #if (DEBUG) 
        getDebugSerial()->println("EEPROM read fail");
      #endif
      
      //TODO EEPROM_READ_ERROR
      return -1;
    }
    
    // set laod address little endian
    // gotta divide by 2
    addr[0] = ((currentAddress - startAddress) / 2) & 0xff;
    addr[1] = (((currentAddress - startAddress) / 2) >> 8) & 0xff;
    
    #if (VERBOSE && DEBUG)
      getDebugSerial()->print("send page len: "); getDebugSerial()->println(len, DEC);            
      dumpBuffer(buffer + 3, "read from eeprom->", len);
    #endif
                  
    if (sendPageToOptiboot(addr, buffer, len) == -1) {
      #if (DEBUG) 
        getDebugSerial()->println("send page fail");
      #endif  

      return -1;
    }
    
    currentAddress+= len;
  }
  
  if (sendToOptiboot(STK_LEAVE_PROGMODE, buffer, 0, 0) == -1) {
    #if (DEBUG) 
      getDebugSerial()->println("STK_LEAVE_PROGMODE failure");
    #endif  

    return -1;       
  }
  
  // SUCCESS!!
  #if (DEBUG)
    getDebugSerial()->print("Flashed in "); getDebugSerial()->print(millis() - start, DEC); getDebugSerial()->println("ms");
  #endif
      
  return 0;
}
 
bool RemoteUploader::isProgrammingPacket(uint8_t packet[], uint8_t packetLength) {
  if (packetLength >= 4 && packet[0] == MAGIC_BYTE1 && packet[1] == MAGIC_BYTE2) {          
    return true;
  }
  
  return false;
}

bool RemoteUploader::isFlashPacket(uint8_t packet[]) {
  if (packet[2] == CONTROL_FLASH_START) {
    return true;
  }
  
  return false;
}

// process packet and return reply code for host
int RemoteUploader::process(uint8_t packet[]) {
    // if programming start magic packet is received:
    // reset the target arduino.. determine the neecessary delay
    // send data portion of packets to serial.. but look for magic word on packet so we know it's programming data
    // NOTE: any programs that send to this radio should be shutdown or the programming would hose the arduino
    // on final packet do any verification to see if it boots

//    #if (DEBUG) 
//      dumpBuffer(packet, "packet", packetLength);
//    #endif
        
      // 3 bytes for head + at least one programming
      // TODO also check rx.getData(2) is one of CONTROL_PROG_X for extra measure of ensuring we are not acting on a application packet
        // echo * for each programming packet
        #if (DEBUG) 
          getDebugSerial()->print("*");
        #endif
      
        if (packet[2] == CONTROL_PROG_REQUEST) {
         // start
          #if (DEBUG) 
            getDebugSerial()->println("Received start packet");
          #endif  
          
          if (inProgramming) {
            // TODO print warning: already in prog. reset and continue
            #if (DEBUG) 
              getDebugSerial()->println("Error: in prog");
            #endif
          }

          // reset state
          reset();
          
          programmingStartMillis = millis();
          inProgramming = true; 
          packetCount = 0;

//        MAGIC_BYTE1, 
//        MAGIC_BYTE2, 
//        CONTROL_PROG_REQUEST, 
//        9, // length, including header
//        (sizeInBytes >> 8) & 0xff, 
//        sizeInBytes & 0xff, 
//        (numPages >> 8) & 0xff, 
//        numPages & 0xff,
//        bytesPerPage      
          // program size
          programSize = (packet[4] << 8) + packet[5];
          // num packets to expect
          numPackets = packet[6] << 8 + packet[7];   
        } else if (packet[2] == CONTROL_PROG_DATA && inProgramming) {
          packetCount++;
          
          // data starts at 16 (12 bytes xbee header + data header
          //dumpBuffer(xbee.getResponse().getFrameData() + 16, "packet", xbee.getResponse().getFrameDataLength() - 16);
          // header
//        MAGIC_BYTE1, 
//        MAGIC_BYTE2, 
//        CONTROL_PROG_DATA, 
//        packetLength + 6, //length + 6 bytes for header
//        (address >> 8) & 0xff, 
//        address & 0xff
          int packetLen = packet[3];
          int address = (packet[4] << 8) + packet[5]; 
          
          //getDebugSerial()->print("addr msb "); getDebugSerial()->print(rx.getData(3), DEC); getDebugSerial()->print(" addr lsb "); getDebugSerial()->println(rx.getData(4), DEC);
          //getDebugSerial()->print("curaddr msb "); getDebugSerial()->print(((currentEEPROMAddress - EEPROM_OFFSET_ADDRESS) >> 8) & 0xff, DEC); getDebugSerial()->print(" addr lsb "); getDebugSerial()->println((currentEEPROMAddress - EEPROM_OFFSET_ADDRESS) & 0xff, DEC);
          
          // now write page to eeprom

          // check if the address of this packet aligns with the last write to eeprom. it could be a resend and that's ok
          if (currentEEPROMAddress < (address + EEPROM_OFFSET_ADDRESS)) {
            #if (DEBUG)
              getDebugSerial()->print("WARN: expected address "); getDebugSerial()->print(currentEEPROMAddress, DEC); getDebugSerial()->print(" but got "); getDebugSerial()->println(address + EEPROM_OFFSET_ADDRESS, DEC);
            #endif
          } else if (currentEEPROMAddress > (address + EEPROM_OFFSET_ADDRESS)) {
            // attempt to write beyond current eeprom address
            // this would result in a gap in data and would ultimately fail, so reject
            #if (DEBUG)
              getDebugSerial()->print("ERROR: attempt to write @ address "); getDebugSerial()->print((address + EEPROM_OFFSET_ADDRESS) & 0xff, DEC); getDebugSerial()->print(" but current address @ "); getDebugSerial()->println(currentEEPROMAddress & 0xff, DEC);            
            #endif
            
            return START_OVER;
          }

          // NOTE we've made it idempotent in case we get retries
          // TODO validate it's in range            
          currentEEPROMAddress = address + EEPROM_OFFSET_ADDRESS;

//          #if (DEBUG)
//            getDebugSerial()->print("curaddr "); getDebugSerial()->println(currentEEPROMAddress, DEC); 
//          #endif

          //dumpBuffer(packet + 5, "packet", packetLength - 5);
            
          uint8_t dataLen = packetLen - PROG_DATA_HEADER_SIZE;
            
          if (eeprom->write(currentEEPROMAddress, packet + PROG_DATA_HEADER_SIZE, dataLen) != 0) {
            #if (DEBUG) 
              getDebugSerial()->println("EEPROM write failure");
            #endif  
            
            return EEPROM_WRITE_ERROR;
          }
          
          currentEEPROMAddress+= dataLen;

//          #if (DEBUG)
//            getDebugSerial()->print("len is "); getDebugSerial()->println(len, DEC);
//            getDebugSerial()->print("addr+len "); getDebugSerial()->println(currentEEPROMAddress, DEC); 
//          #endif
          
          if (packetCount == numPackets) {
            // should be last packet but maybe not if we got retries 
          }
          
          // TODO write a checksum in eeprom header so we can verify prior to flashing                          
          // prog data
        } else if (isFlashPacket(packet) && inProgramming) {
          // done verify we got expected # packets

//        MAGIC_BYTE1, 
//        MAGIC_BYTE2, 
//        CONTROL_START_FLASH, 
//        packetLength + 6, //length + 6 bytes for header
//        (progSize >> 8) & 0xff, 
//        progSize & 0xff

          #if (DEBUG) 
            getDebugSerial()->println("");
          #endif  

        // debug remove
        #if (DEBUG) 
          getDebugSerial()->println("Flash start packet");
        #endif
        
          // TODO verify that's what we've received            
          // NOTE redundant we have programSize
          int psize = (packet[4] << 8) + packet[5];
                    
          if (psize != currentEEPROMAddress - EEPROM_OFFSET_ADDRESS) {
            #if (DEBUG) 
              getDebugSerial()->print("psize "); getDebugSerial()->print(psize, HEX); getDebugSerial()->print(",cur addr "); getDebugSerial()->println(currentEEPROMAddress - EEPROM_OFFSET_ADDRESS, HEX);
            #endif              
            // TODO make codes more explicit
            return START_OVER;             
          } else if (psize != programSize) {
            #if (DEBUG) 
              getDebugSerial()->println("psize != programSize");
            #endif              
            
            return START_OVER;             
          }         

          // set to optiboot speed
          getProgrammerSerial()->begin(OPTIBOOT_BAUD_RATE);
  
          if (flash(EEPROM_OFFSET_ADDRESS, programSize) != 0) {
            #if (DEBUG)
              getDebugSerial()->println("Flash failure");
            #endif              
            return FLASH_ERROR;
          }
                    
          // reset everything
          reset();
        } else {
          // sync error, not expecting prog data   
          // TODO send error. client needs to start over
          #if (DEBUG) 
            getDebugSerial()->println("not-in-prog");
          #endif  
          
          return START_OVER;
        }
        
        // update so we know when to timeout
        lastUpdateAtMillis = millis();
        return OK;
}

int RemoteUploader::setup(HardwareSerial* _serial, extEEPROM* _eeprom, uint8_t _resetPin) {
  eeprom = _eeprom;
  progammerSerial = _serial;
  resetPin = _resetPin;      
  pinMode(resetPin, OUTPUT);

  if (eeprom->begin(twiClock400kHz) != 0) {
    #if (DEBUG) 
      getDebugSerial()->println("eeprom failure");
    #endif  
    
    return EEPROM_ERROR;
  } 
  
  // only necessary for leonardo. safe for others
  while (!Serial);
  
  return 0;
}
