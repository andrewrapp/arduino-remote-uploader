#include <SoftwareSerial.h>
#include <XBee.h>

#define MAGIC_BYTE1 0xef
#define MAGIC_BYTE2 0xac

#define START_PROGRAMMING 0xa0
#define PROGRAM_DATA 0xa1
#define STOP_PROGRAMMING 0xa2


// FAIL -- arduino programming is not as simple as sending data after reset. there's a protocol
// See https://github.com/sparkfun/Tomatoless_Boots/blob/master/tomatoless.device.nut
// http://www.ladyada.net/make/xbee/arduino.html
// http://makezine.com/projects/diy-arduino-bluetooth-programming-shield/
// Maybe could still be done but have lots of doubts. would need to implement avrdude protocol in ardruino
// Also need to change baud rate to 19200, or 56K depending on atmega

/**
XAWP - xbee arduino wireless programming (in api mode)
There are some tutorials for programming an arduino with an xbee in transparent mode. This is not that. This is about using API mode.

For series 1 it should be possible, in theory, to place the XBee in transparent mode and reset, then start sending the program. This is just speculation however.
But with series2, it's not possible to switch between trasnparent and api via configuration, since separate firmware is required.
*/

XBee xbee = XBee();
XBeeResponse response = XBeeResponse();
ZBRxResponse rx = ZBRxResponse();

// WIRING:
// prog arduino=>target arduino: 4->serial rx, 5->serial tx, 10->RESET
// xbee connected to prog arduino via shield
// the target arduino should use xbee api with softserial to the aforementioned pins, as if it were connected directory to a xbee radio

const int ssTx = 4;
const int ssRx = 5;
const int resetPin = 10;

bool isProgramming = false;

//Since Arduino 1.0 we have the superior softserial implementation: NewSoftSerial
// Remember to connect all devices to a common Ground: XBee, Arduino and USB-Serial device
SoftwareSerial nss(ssTx, ssRx);

void setup() {
  // start usb serial on leonardo for debugging
  Serial.begin(9600);
  // leonardo wait for serial
  while (!Serial);
  
  pinMode(resetPin, OUTPUT);
  
  // we only have one Serial port (UART) so need nss for relaying packets to the other Arduino
  nss.begin(9600);
    
  // start UART serial for xbee on leonardo
  Serial1.begin(9600);
  xbee.setSerial(Serial1);
  
  Serial.println("XAWP Ready");
}

void sendData(ZBRxResponse rx, int offset) {
  for (int i = offset; i < rx.getDataLength(); i++) {
    // send to Arduino but don't escape!
    sendByte(rx.getData(i), false);
  } 
  
  nss.flush();
}
// borrowed from xbee api. send bytes with proper escaping
void sendByte(uint8_t b, bool escape) {
  if (escape && (b == START_BYTE || b == ESCAPE || b == XON || b == XOFF)) {
    nss.print(ESCAPE, DEC);
    nss.print(b ^ 0x20, DEC);
  } else {
    nss.print(b, DEC);
  }
}

void loop() {   
  xbee.readPacket();

  // target arduino must be a 328 that is programmed via serial port
  
  if (xbee.getResponse().isAvailable()) {
    // if programming start magic packet is received:
    // reset the target arduino.. determine the neecessary delay
    // send data portion of packets to serial.. but look for magic word on packet so we know it's programming data
    // NOTE: any programs that send to this radio should be shutdown or the programming would hose the arduino
    // on final packet do any verification to see if it boots

    if (xbee.getResponse().getApiId() == ZB_RX_RESPONSE) {
      bool prog = false;
      
      // now fill our zb rx class
      xbee.getResponse().getZBRxResponse(rx);
      
      // TODO check for packet errors
      

      // 3 bytes for head + at least one programming
      if (rx.getDataLength() >= 4) {
        if (rx.getData(0) == MAGIC_BYTE1 && rx.getData(1) == MAGIC_BYTE2) {
          prog = true;
          
          if (rx.getData(2) == START_PROGRAMMING) {
            Serial.println("Received programming start packet");
            
            // set serial to match bootloader speed
            nss.begin(19200);
            
            if (isProgramming) {
              Serial.println("Error: never received programming stop packet"); 
            }
            
            isProgramming = true;
            
            // start
            // set reset pin low
            digitalWrite(resetPin, LOW);
            // 2.5us min
            delayMicroseconds(5);
            digitalWrite(resetPin, HIGH);
            
            sendData(rx, 3);
          } else if (rx.getData(2) == PROGRAM_DATA) {
            // send programmingdata
            sendData(rx, 3);
          } else if (rx.getData(2) == STOP_PROGRAMMING) {
            sendData(rx, 3);
            isProgramming = false;
            Serial.println("Received programming stop packet");
            
            // reset to xbee speed
            nss.begin(9600);
          }
        }
      }
      
      if (!prog) {
        // not programming packet, so proxy to Arduino via nss
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
       
       nss.flush();
      }
    }
    
    // if not programming packet, relay exact bytes to the arduino. need to figure out how to get from library
    // the target sketch should ignore any programming packets it receives as that is an indication of a failed programming attempt
    
    // we can use a 2nd softserial to debug from target arduino to prog arduino
  } else if (xbee.getResponse().isError()) {
    Serial.print("RX error in loop:");
    Serial.println(xbee.getResponse().getErrorCode(), DEC);
  }  
  
  // pass all data from remote out the serial port
  // we don't need to do anything with these packets
  while (nss.available()) {
    Serial1.print(nss.read()); 
  }
}
