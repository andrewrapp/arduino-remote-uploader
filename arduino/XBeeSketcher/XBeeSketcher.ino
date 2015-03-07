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
 
#include "eeprom_flasher.h"

#include <XBee.h>
#include <SoftwareSerial.h>
#include <extEEPROM.h>
#include <Wire.h>

#define XBEE_BAUD_RATE 9600

// Specify the XBee coordinator address to send ACKs
const uint32_t COORD_MSB_ADDRESS = 0x0013a200;
const uint32_t COORD_LSB_ADDRESS = 0x408b98fe;

XBee xbee = XBee();
XBeeResponse response = XBeeResponse();
ZBRxResponse rx = ZBRxResponse();

// Coordinator/XMPP Gateway
XBeeAddress64 addr64 = XBeeAddress64(COORD_MSB_ADDRESS, COORD_LSB_ADDRESS);
ZBTxRequest tx = ZBTxRequest(addr64, xbeeTxPayload, sizeof(xbeeTxPayload));
ZBTxStatusResponse txStatus = ZBTxStatusResponse();

void setup() {
  int setup_success = setup_core();

  // TODO if setup_success != OK send error programming attempt
  // we only have one Serial port (UART) so need nss for XBee
  nss.begin(XBEE_BAUD_RATE);  
  xbee.setSerial(nss);
}

void checkTimeout() {
  if (in_prog && last_packet > 0 && (millis() - last_packet) > PROG_TIMEOUT) {
    // timeout
    #if (USBDEBUG || NSSDEBUG)
      getDebugSerial()->println("Prog timeout");
    #endif  
    // tell host to start over
    sendReply(TIMEOUT);
    
    prog_reset();
  }
}

void handleProxy() {
  // don't need to test in_prog. if in prog we are just collecting packets so can keep relaying. when flashing, it's blocking so will never get here
  // forward packets from target out the radio
  if (PROXY_SERIAL) {
    while (getProgrammerSerial()->available() > 0) {
      int b = getProgrammerSerial()->read();
      getXBeeSerial()->write(b); 
    }      
  }
}

// TODO send version
int sendReply(uint8_t status) {
  xbeeTxPayload[0] = MAGIC_BYTE1;
  xbeeTxPayload[1] = MAGIC_BYTE2;
  xbeeTxPayload[2] = status;
  
  // TODO send with magic packet host can differentiate between relayed packets and programming ACKS
  xbee.send(tx);
  
  // after send a tx request, we expect a status response
  // wait up to half second for the status response
  if (xbee.readPacket(1000)) {    
    if (xbee.getResponse().getApiId() == ZB_TX_STATUS_RESPONSE) {
      xbee.getResponse().getZBTxStatusResponse(txStatus);

      // get the delivery status, the fifth byte
      if (txStatus.isSuccess()) {
        // good
        return 0;
      } else {
        #if (USBDEBUG || NSSDEBUG) 
          getDebugSerial()->println("TX fail");
        #endif  
      }
    }      
  } else if (xbee.getResponse().isError()) {
    #if (USBDEBUG || NSSDEBUG)
      getDebugSerial()->print("TX error:");  
      getDebugSerial()->print(xbee.getResponse().getErrorCode());
    #endif
  } else {
    #if (USBDEBUG || NSSDEBUG) 
      getDebugSerial()->println("TX timeout");
    #endif  
  } 
  
  return -1;
}


// borrowed from xbee api. send bytes with proper escaping
void send_xbee_packet(uint8_t b, bool escape) {
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
  
  #if (USBDEBUG || NSSDEBUG) 
    getDebugSerial()->println("Forwarding packet");    
  #endif
        
  // send start byte, length, api, then frame data + checksum
  send_xbee_packet(START_BYTE, false);
  send_xbee_packet(xbee.getResponse().getMsbLength(), true);
  send_xbee_packet(xbee.getResponse().getLsbLength(), true);        
  send_xbee_packet(xbee.getResponse().getApiId(), true);

  uint8_t* frameData = xbee.getResponse().getFrameData();
   
  for (int i = 0; i < xbee.getResponse().getFrameDataLength(); i++) {
    send_xbee_packet(*(frameData + i), true);
  }
   
   send_xbee_packet(xbee.getResponse().getChecksum(), true);  
}

void loop() {  
  xbee.readPacket();
  
  if (xbee.getResponse().isAvailable()) {  
    // if not programming packet, relay exact bytes to the arduino. need to figure out how to get from library
    // NOTE the target sketch should ignore any programming packets it receives as that is an indication of a failed programming attempt
    
      if (xbee.getResponse().getApiId() == ZB_RX_RESPONSE) {      
        // now fill our zb rx class
        xbee.getResponse().getZBRxResponse(rx);
        
        if (rx.getDataLength() > 4 && isProgrammingPacket(xbee.getResponse().getFrameData() + rx.getDataOffset(), rx.getDataLength())) {
          // send the packet array, length to be processed
          int response = handlePacket(xbee.getResponse().getFrameData() + rx.getDataOffset(), rx.getDataLength());
          
          if (response != OK) {
            prog_reset();
          }
          
          sendReply(response);          
        } else {
          // not a programming packet. forward along
          if (PROXY_SERIAL) {
            forwardPacket();                  
          }          
        }
      }
  } else if (xbee.getResponse().isError()) {
    #if (USBDEBUG || NSSDEBUG) 
      getDebugSerial()->print("RX error: ");
      getDebugSerial()->println(xbee.getResponse().getErrorCode(), DEC);
    #endif  
  }  
  
  checkTimeout();
  handleProxy();
}
