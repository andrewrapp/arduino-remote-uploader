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


/*
Microchip 24LC256
Arduino Analog pin 4 - SDA - EEPROM pin 5
Arduino Analog pin 5 - SCL - EEPROM pin 6
Arduino 5V           - VCC - EEPROM pin 8
Arduino GND          - VSS - EEPROM pin 4 

* pin 1 is has the dot, on the notched end, if you were wondering

Pin 1,2,3 of the eeprom must be connect to GND too unless other address is used, see datasheet. Pin 7 (write protect) should also be connected to GND
*/


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

int packetCount = 0;
int packets = 0;
int size = 0;
bool prog = false;
long prog_start = 0;
long last_packet = 0;

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
          
          if (rx.getData(2) == 0x10) {
           // start

            getDebugSerial()->println("Received programming start packet");
            
            if (prog) {
              getDebugSerial()->println("Error: never received programming stop packet"); 
            }

             // tell Arduino it's about to be flashed
              // forwardPacket();
            
            prog_start = millis();
            prog = true; 
            
            // size in bytes
            size = rx.getData(3) << 8 + rx.getData(4);
            // num packets to be sent
            packets = rx.getData(5) << 8 + rx.getData(6);            
          } else if (rx.getData(2) == 0x20 && prog) {
            packetCount++;
            
            // write to eeprom
            for (int i = 7; i < rx.getDataLength(); i++) {
              //rx.getData(i)
            }  
            
            if (packetCount == packets) {
              // last packet
              
            // done do any verification and reply back
            // call function to read from eeprom and flash
            
              // start flashing
              
              
              
              
              
              prog = false;
            } else if (packetCount > packets) {
              // error
            }
            
            // prog data
          } else if (rx.getData(2) == 0x40 && prog) {
            // done verify we got expected # packets
            getDebugSerial()->println("Received programming stop packet");            
            prog = false;
          } else {
            // sync error, not expecting prog data
            
          }
        }
        
        last_packet = millis();
      }
      
      if (prog == false) {
        forwardPacket();
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
  
  if (prog && millis() - last_packet > 5000) {
    // timeout
    getDebugSerial()->println("Programming timeout");    
    prog = false;
    // clear eeprom
  }
  
  if (prog == false) {
    // check if this has magic bytes before forwarding
    
    // pass all data (xbee packets) from remote out the xbee serial port
    // we don't need to do anything with these packets
    while (getProgrammerSerial()->available() > 0) {
      getXBeeSerial()->write(getProgrammerSerial()->read()); 
    }    
  }
}
