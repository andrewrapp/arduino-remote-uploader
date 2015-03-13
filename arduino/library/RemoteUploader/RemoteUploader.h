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
 
#ifndef RemoteUploader_h
#define RemoteUploader_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "Arduino.h"
#else
	#include "WProgram.h"
#endif

#include <inttypes.h>
#include <SoftwareSerial.h>
#include <extEEPROM.h>

// TODO how do we pass in DEBUG as a parameter??

// Enable debugging statments on any arduino that has multiple serial ports. Must also call setDebugSerial()
// Never set to Serial on atmega328/168 as it needs Serial for programming and it will certainly fail on flash()
// IMPORTANT: you must have the serial monitor open when usb debug enabled or will fail after a few packets!
#define DEBUG false

// Currently it goes out of memory on atmega328/168 with VERBOSE true. TODO shorten strings so it doesn't go out of memory
// Must also enable a debug option (DEBUG or NSSDEBUG) with VERBOSE true. With atmega328/168 you may only use NSSDEBUG as the only serial port is for flashing
#define VERBOSE false

// The remaining config should be fine for vast majority of cases
// Currently it goes out of memory on atmega328/168 with VERBOSE true. TODO shorten strings so it doesn't go out of memory
// Must also enable a debug option (DEBUG or NSSDEBUG) with VERBOSE true. With atmega328/168 you may only use NSSDEBUG as the only serial port is for flashing

// only 115200 works with optiboot
#define OPTIBOOT_BAUD_RATE 115200
// how long to wait for a reply from optiboot before timeout (ms)
#define OPTIBOOT_READ_TIMEOUT 1000

#define PROG_PAGE_SIZE 128
// this can be reduced to the maximum packet size + header bytes
// memory shouldn't be an issue on the programmer since it only should ever run this sketch!

#define BUFFER_SIZE PROG_PAGE_SIZE + 3
#define READ_BUFFER_SIZE PROG_PAGE_SIZE + 3

// TODO not implemented yet
#define PROG_PAGE_RETRIES 2
// the address to start writing the hex to the eeprom
#define EEPROM_OFFSET_ADDRESS 16

// ==================================================================END CONFIG ==================================================================

// TODO define negative error codes for sketch only. translate to byte error code and send to XBee

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

// error codes returned by process
// every packet must return exactly one reply: OK or NOT OK. NOT OK is anything > 1
// TODO make sure only one reply code is sent!
#define OK 1
//got prog data but no start. host needs to start over
#define START_OVER 2
#define TIMEOUT 3
#define FLASH_ERROR 4
#define EEPROM_ERROR 5
#define EEPROM_WRITE_ERROR 6
// TODO these should be bit sets on FLASH_ERROR
#define EEPROM_READ_ERROR 7
// serial lines not connected or reset pin not connected
#define NOBOOTLOADER_ERROR 8
#define VERIFY_PAGE_ERROR 9
#define ADDRESS_SKIP_ERROR 0xa

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

class RemoteUploader {
public:
	RemoteUploader();
	void dumpBuffer(uint8_t arr[], char context[], uint8_t len);	
	int process(uint8_t packet[]);
	int setup(HardwareSerial* _serial, extEEPROM* _eeprom, uint8_t _resetPin);
	bool inProgrammingMode();
	long getLastPacketMillis();
	bool isTimeout();
	void reset();
	bool isProgrammingPacket(uint8_t packet[], uint8_t packet_length);
	bool isFlashPacket(uint8_t packet[]);
	HardwareSerial* getProgrammerSerial();	
	// can only use DEBUG with Leonardo or other variant that supports multiple serial ports
	#if (DEBUG)
	  Stream* getDebugSerial();
	  void setDebugSerial(Stream* debugSerial);
	#endif	
private:
	void clearRead();
	int readOptibootReply(uint8_t len, int timeout);
	int sendToOptiboot(uint8_t command, uint8_t *arr, uint8_t len, uint8_t response_length);
	int flashInit();
	int sendPageToOptiboot(uint8_t *addr, uint8_t *buf, uint8_t data_len);
	void bounce();
	int flash(int start_address, int size);
	extEEPROM* eeprom;
	// ** IMPORTANT! **
	// For Leonardo use Serial1 (UART) or it will try to program through usb-serial
	// For atmega328 use Serial
	// For megas other e.g. Serial2 should work -- UNTESTED!
	HardwareSerial* progammerSerial;
	Stream* debugSerial;
	uint8_t cmd_buffer[1];
	uint8_t addr[2];
	// TODO revisit these sizes
	uint8_t buffer[BUFFER_SIZE];
	uint8_t readBuffer[READ_BUFFER_SIZE];
	uint8_t resetPin;
	int packetCount;
	int numPackets;
	int programSize;
	bool inProgramming;
	long programmingStartMillis;
	long lastUpdateAtMillis;
	int currentEEPROMAddress;
	int maxEEPROMAddress;
	uint8_t bytesPerPacket;
	long programmingTimeout;
};

#endif // guard
