

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

// TODO support XBee series1
// should we proxy serial rx/tx to softserial (xbee). if you want to use the XBee from the application arduino set to true -- if only using xbee for programming set to false
#define PROXY_SERIAL true
#define XBEE_BAUD_RATE 9600

// TODO send with start header
#define ACK_TIMEOUT 1000

// these can be swapped to any other free digital pins
#define XBEE_SOFTSERIAL_TX_PIN 8
#define XBEE_SOFTSERIAL_RX_PIN 7

// Specify the XBee coordinator address to send ACKs
const uint32_t COORD_MSB_ADDRESS = 0x0013a200;
const uint32_t COORD_LSB_ADDRESS = 0x408b98fe;

XBee xbee = XBee();
XBeeResponse response = XBeeResponse();
ZBRxResponse rx = ZBRxResponse();

// format magic bytes, status, id1, id2
uint8_t xbeeTxPayload[] = { MAGIC_BYTE1, MAGIC_BYTE2, 0, 0, 0 };

// Coordinator/XMPP Gateway
XBeeAddress64 addr64 = XBeeAddress64(COORD_MSB_ADDRESS, COORD_LSB_ADDRESS);
ZBTxRequest tx = ZBTxRequest(addr64, xbeeTxPayload, sizeof(xbeeTxPayload));
ZBTxStatusResponse txStatus = ZBTxStatusResponse();

//Since Arduino 1.0 we have the superior softserial implementation: NewSoftSerial
// Remember to connect all devices to a common Ground: XBee, Arduino and USB-Serial device
SoftwareSerial nss(XBEE_SOFTSERIAL_TX_PIN, XBEE_SOFTSERIAL_RX_PIN);

RemoteUploader remoteUploader = RemoteUploader();

extEEPROM eeprom = extEEPROM(kbits_256, 1, 64);

Stream* getXBeeSerial() {
  return &nss;  
}

void setup() {
  // setup uploader with the serial, eeprom and reset pin
  remoteUploader.setup(&Serial1, &eeprom, 9);
  // use usb-serial for debug
  remoteUploader.setDebugSerial(&Serial);  
  // TODO if setup_success != OK send error programming attempt
  // we only have one Serial port (UART) so need nss for XBee
  
  nss.begin(XBEE_BAUD_RATE);  
  xbee.setSerial(nss);
  
  if (PROXY_SERIAL) {
    remoteUploader.getProgrammerSerial()->begin(XBEE_BAUD_RATE);    
  } 
  
  #if (USBDEBUG || NSSDEBUG) 
    if (setup == 0) {
      getDebugSerial()->println("Ready");
    }
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
  xbee.send(tx);
  
  // after send a tx request, we expect a status response
  // wait up to half second for the status response
  if (xbee.readPacket(ACK_TIMEOUT)) {    
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
      // starting to see lots of these. check wire connections are secure
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

// TODO ack needs to send the id (packet #) otherwise we may be getting a different ack, right?

void loop() {  
  xbee.readPacket();
  
  if (xbee.getResponse().isAvailable()) {  
    // if not programming packet, relay exact bytes to the arduino. need to figure out how to get from library
    // NOTE the target sketch should ignore any programming packets it receives as that is an indication of a failed programming attempt
    
      if (xbee.getResponse().getApiId() == ZB_RX_RESPONSE) {      
        // now fill our zb rx class
        xbee.getResponse().getZBRxResponse(rx);
        
        // pointer of data position in response
        uint8_t *packet = xbee.getResponse().getFrameData() + rx.getDataOffset();
        
        if (rx.getDataLength() > 4 && remoteUploader.isProgrammingPacket(packet, rx.getDataLength())) {
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
