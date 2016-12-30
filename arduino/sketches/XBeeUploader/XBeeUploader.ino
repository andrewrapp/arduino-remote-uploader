

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
 
#include <XBee.h>
#include <SoftwareSerial.h>
#include <extEEPROM.h>
#include <Wire.h>
#include <RemoteUploader.h>

// NOTE: Leonardo seems to have no problem powering the xbee ~50ma and and a Diecimila

// Set to true to forward serial (xbee traffic) to other Arduino
#define PROXY_SERIAL true
#define XBEE_BAUD_RATE 9600
#define USBDEBUG false

#define SERIES1 1
#define SERIES2 2

// TODO send with start header
#define ACK_TIMEOUT 1000

// these can be swapped to any other free digital pins
#define XBEE_SOFTSERIAL_RX_PIN 7
#define XBEE_SOFTSERIAL_TX_PIN 8
#define RESET_PIN 9

// set a dummy address. all tx will be sent to the sender via getRemoteAddress
const uint32_t COORD_MSB_ADDRESS = 0;
const uint32_t COORD_LSB_ADDRESS = 0;

XBee xbee = XBee();
XBeeResponse response = XBeeResponse();
// series 1 rx objects
Rx64Response rx1 = Rx64Response();
// series 2
ZBRxResponse rx2 = ZBRxResponse();

// format magic bytes, status, id1, id2
uint8_t xbeeTxPayload[] = { MAGIC_BYTE1, MAGIC_BYTE2, 0, 0, 0 };

// TODO use ifdef to either include series 1 or 2 but not both

// Coordinator Gateway
XBeeAddress64 addr64 = XBeeAddress64(COORD_MSB_ADDRESS, COORD_LSB_ADDRESS);
// series 1
Tx64Request tx1 = Tx64Request(addr64, xbeeTxPayload, sizeof(xbeeTxPayload));
// series 2
ZBTxRequest tx2 = ZBTxRequest(addr64, xbeeTxPayload, sizeof(xbeeTxPayload));

// status response
// series 1
TxStatusResponse txStatus1 = TxStatusResponse();
// series 2
ZBTxStatusResponse txStatus2 = ZBTxStatusResponse();

//Since Arduino 1.0 we have the superior softserial implementation: NewSoftSerial
// Remember to connect all devices to a common Ground: XBee, Arduino and USB-Serial device
SoftwareSerial nss(XBEE_SOFTSERIAL_TX_PIN, XBEE_SOFTSERIAL_RX_PIN); // RX, TX

RemoteUploader remoteUploader = RemoteUploader();

extEEPROM eeprom = extEEPROM(kbits_256, 1, 64);

// keep track radio type: series 1 or 2
uint8_t series = 0;

Stream* getXBeeSerial() {
  return &nss;  
}

void setup() {
  // for atmega328/168 use &Serial
  // do not set baud rate as the uploader does this
  remoteUploader.setup(&Serial, &eeprom, RESET_PIN);  
  // for Leonardo must use Serial1
  //remoteUploader.setup(&Serial1, &eeprom, RESET_PIN);
  //configure debug if an additional Serial port is available. Use Serial with Leonardo
  // for some reason I can only program with a Leonardo when the serial console is open, even if I'm not using debug!
  //remoteUploader.setDebugSerial(&Serial);

  // TODO if setup_success != OK send error programming attempt
  // we only have one Serial port (UART) so need nss for XBee
  
  nss.begin(XBEE_BAUD_RATE);  
  xbee.setSerial(nss);
  
  #if (USBDEBUG || NSSDEBUG) 
    remoteUploader.getDebugSerial()->println("Ready");
  #endif
}

// TODO move to library.. tell library of the proxySerial port
void handleProxy() {
  // don't need to test in_prog. if in prog we are just collecting packets so can keep relaying. when flashing, it's blocking so will never get here
  // forward packets from target out the radio
  if (PROXY_SERIAL) {
    while (remoteUploader.getProgrammerSerial()->available() > 0) {
      int b = remoteUploader.getProgrammerSerial()->read();
      getXBeeSerial()->write(b); 
    }      
  }
}

// TODO send version
int sendReply(uint8_t status, uint16_t id) {
  xbeeTxPayload[0] = MAGIC_BYTE1;
  xbeeTxPayload[1] = MAGIC_BYTE2;
  xbeeTxPayload[2] = status;
  xbeeTxPayload[3] = (id >> 8) & 0xff;
  xbeeTxPayload[4] = id & 0xff;
  
  // TODO send with magic packet host can differentiate between relayed packets and programming ACKS
  if (series == SERIES1) {
    xbee.send(tx1);        
  } else if (series == SERIES2) {
    // send reply to sender xbee
    tx2.setAddress64(rx2.getRemoteAddress64());
    xbee.send(tx2);    
  }
  
  // after send a tx request, we expect a status response
  // wait up to half second for the status response
  if (xbee.readPacket(ACK_TIMEOUT)) {    
    if (xbee.getResponse().getApiId() == ZB_TX_STATUS_RESPONSE) {
      // series 2
      xbee.getResponse().getZBTxStatusResponse(txStatus2);

      // get the delivery status, the fifth byte
      if (txStatus2.isSuccess()) {
        return 0;
      } else {
        #if (USBDEBUG || NSSDEBUG) 
          remoteUploader.getDebugSerial()->println("TX fail");
        #endif  
      }
    } else if (xbee.getResponse().getApiId() == TX_STATUS_RESPONSE) {
      // series 1
      xbee.getResponse().getTxStatusResponse(txStatus1);
        
      if (txStatus1.getStatus() == SUCCESS) {
        return 0;
      } else {
        #if (USBDEBUG || NSSDEBUG) 
          remoteUploader.getDebugSerial()->println("TX fail");
        #endif      
      }  
    }   
  } else if (xbee.getResponse().isError()) {
    #if (USBDEBUG || NSSDEBUG)
      // starting to see lots of these. check wire connections are secure
      remoteUploader.getDebugSerial()->print("TX error:");  
      remoteUploader.getDebugSerial()->print(xbee.getResponse().getErrorCode());
    #endif
  } else {
    #if (USBDEBUG || NSSDEBUG) 
      remoteUploader.getDebugSerial()->println("TX timeout");
    #endif  
  } 
  
  return -1;
}


// borrowed from xbee api. send bytes with proper escaping
void send_xbee_packet(uint8_t b, bool escape) {
  if (escape && (b == START_BYTE || b == ESCAPE || b == XON || b == XOFF)) {
    remoteUploader.getProgrammerSerial()->write(ESCAPE);    
    remoteUploader.getProgrammerSerial()->write(b ^ 0x20);
  } else {
    remoteUploader.getProgrammerSerial()->write(b);
  }
  
  remoteUploader.getProgrammerSerial()->flush();
}

void forwardPacket() {
  // not programming packet, so proxy all xbee traffic to Arduino
  // prob cleaner way to do this if I think about it some more
  
  #if (USBDEBUG || NSSDEBUG) 
    remoteUploader.getDebugSerial()->println("Forwarding packet");    
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

// TODO ack needs to send the id (packet #) otherwise we may be getting a different ack, right?

void loop() {        
  xbee.readPacket();
  
  if (xbee.getResponse().isAvailable()) {  
    // if not programming packet, relay exact bytes to the arduino. need to figure out how to get from library
    // NOTE the target sketch should ignore any programming packets it receives as that is an indication of a failed programming attempt
          
      uint8_t *packet = NULL;
      uint8_t length;
      
      if (xbee.getResponse().getApiId() == ZB_RX_RESPONSE) {  
        // series 2    
        series = SERIES2;
        // now fill our zb rx class
        xbee.getResponse().getZBRxResponse(rx2);
        
        // pointer of data position in response
        packet = xbee.getResponse().getFrameData() + rx2.getDataOffset();
        length = rx2.getDataLength();
      } else if (xbee.getResponse().getApiId() == RX_64_RESPONSE) {
        // series 1
        series = SERIES1;        
        xbee.getResponse().getRx64Response(rx1);      

        // pointer of data position in response
        packet = xbee.getResponse().getFrameData() + rx1.getDataOffset();        
        length = rx1.getDataLength();
      } else {
        // unexpected packet.. ignore
      }

      if (packet != NULL) {
        if (length > 4 && remoteUploader.isProgrammingPacket(packet, length)) {
          // send the packet array, length to be processed
          int response = remoteUploader.process(packet);
              
          // do reset in library
          if (response != OK) {
            remoteUploader.reset();
          }

          sendReply(response, remoteUploader.getPacketId(packet));          
          
          if (remoteUploader.isFlashPacket(packet)) {
            if (PROXY_SERIAL) {
              // we flashed so reset to xbee baud rate for proxying
              remoteUploader.getProgrammerSerial()->begin(XBEE_BAUD_RATE);              
            }
          }          
        } else {
          // not a programming packet. forward along
          if (PROXY_SERIAL) {
            forwardPacket();                  
          }          
        }      
      }
  } else if (xbee.getResponse().isError()) {
    #if (USBDEBUG || NSSDEBUG) 
      remoteUploader.getDebugSerial()->print("RX error: ");
      remoteUploader.getDebugSerial()->println(xbee.getResponse().getErrorCode(), DEC);
    #endif  
  }  
  
  handleProxy();
}
