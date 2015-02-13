#include <SoftwareSerial.h>
#include <XBee.h>

#define MAGIC_BYTE1 0xef
#define MAGIC_BYTE2 0xac

#define START_PROGRAMMING 0xa0
#define PROGRAM_DATA 0xa1
#define STOP_PROGRAMMING 0xa2

XBee xbee = XBee();
XBeeResponse response = XBeeResponse();
ZBRxResponse rx = ZBRxResponse();

// WIRING:
// gotta use XBee with softserial since the serial port is needed for loading the sketch. optiboot needs 115.2 so softserial is not an option

// another configuration that might works is the target has the wireless connection. it writes to the eeprom then tells it's friend to program it
// can two devices connect to the same i2c device

const int softTxPin = 4;
const int softRxPin = 5;
const int resetPin = 10;

bool isProgramming = false;
long prog_start = 0;
long last_packet = 0;

//Since Arduino 1.0 we have the superior softserial implementation: NewSoftSerial
// Remember to connect all devices to a common Ground: XBee, Arduino and USB-Serial device
SoftwareSerial nss(softTxPin, softRxPin);

Stream* getProgrammerSerial() {
  return &Serial1;
}

Stream* getXBeeSerial() {
  return &nss;  
}

Stream* getDebugSerial() {
  return &Serial;  
}

void setup() {
  // start usb serial on leonardo for debugging
  Serial.begin(9600);
  // leonardo wait for serial
  while (!Serial);
  
  pinMode(resetPin, OUTPUT);
  
  // uart is for programming
  Serial1.begin(9600);
  
  // we only have one Serial port (UART) so need nss for XBee
  nss.begin(9600);  
  xbee.setSerial(nss);
  
  getDebugSerial()->println("XAWP Ready");
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

void handleXBee() {
    // if programming start magic packet is received:
    // reset the target arduino.. determine the neecessary delay
    // send data portion of packets to serial.. but look for magic word on packet so we know it's programming data
    // NOTE: any programs that send to this radio should be shutdown or the programming would hose the arduino
    // on final packet do any verification to see if it boots

    if (xbee.getResponse().getApiId() == ZB_RX_RESPONSE) {
      bool prog = false;
      
      // now fill our zb rx class
      xbee.getResponse().getZBRxResponse(rx);
      
      // 3 bytes for head + at least one programming
      if (rx.getDataLength() >= 4) {
        if (rx.getData(0) == MAGIC_BYTE1 && rx.getData(1) == MAGIC_BYTE2) {
          prog = true;
          
          if (rx.getData(2) == START_PROGRAMMING) {
            getDebugSerial()->println("Received programming start packet");
            
            if (isProgramming) {
              getDebugSerial()->println("Error: never received programming stop packet"); 
            }
            
            prog_start = millis();
            isProgramming = true;
          } else if (rx.getData(2) == PROGRAM_DATA) {
            int offset = 3;
            
            // write to eeprom
            for (int i = offset; i < rx.getDataLength(); i++) {
              //rx.getData(i)
            }             
          } else if (rx.getData(2) == STOP_PROGRAMMING) {
            getDebugSerial()->println("Received programming stop packet");
            // done do any verification and reply back
            // call function to read from eeprom and flash
            
            isProgramming = false;
          }
        }
        
        last_packet = millis();
      }
      
      if (isProgramming == false) {
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
    }  
}

void loop() {   
  xbee.readPacket();

  // target arduino must be a 328 that is programmed via serial port
  
  if (xbee.getResponse().isAvailable()) {  
    // if not programming packet, relay exact bytes to the arduino. need to figure out how to get from library
    // NOTE the target sketch should ignore any programming packets it receives as that is an indication of a failed programming attempt
  } else if (xbee.getResponse().isError()) {
    getDebugSerial()->println("RX error in loop:");
    getDebugSerial()->println(xbee.getResponse().getErrorCode(), DEC);
  }  
  
  if (isProgramming && millis() - last_packet > 5000) {
    // timeout
    isProgramming = false;
    // clear eeprom
  }
  
  if (isProgramming == false) {
    // pass all data (xbee packets) from remote out the xbee serial port
    // we don't need to do anything with these packets
    while (getProgrammerSerial()->available() > 0) {
      getXBeeSerial()->write(getProgrammerSerial()->read()); 
    }    
  }
}
