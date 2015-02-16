//#include <SoftwareSerial.h>
#include <XBee.h>

// REMEMBER TO SET BOARD TYPE MATCH THE TARGET ARDUINO (IE OPTIBOOT XXXX) WHEN COMPILING SKETCHES TO BE SENT TO SKETCHER
// HIT VERIFY, THIS COMPILES IT, GET THE PATH, EG
// /var/folders/g1/vflh_srj3gb8zvpx_r5b9phw0000gn/T/build6709388494422663259.tmp/TestXBeeOnTarget.cpp.hex 
// Send with loader

// Simple sketch that demonstrates the ability of the application arduino to receive xbee packets via the programmer arduino

// for leonardo nss test
//const int softTxPin = 11;
//const int softRxPin = 12;
//SoftwareSerial nss(softTxPin, softRxPin);

XBee xbee = XBee();
XBeeResponse response = XBeeResponse();
ZBRxResponse rx = ZBRxResponse();

uint8_t xbeeTxPayload[] = { 8 };
uint32_t COORD_MSB_ADDRESS = 0x0013a200;
uint32_t COORD_LSB_ADDRESS = 0x408b98fe;

// Coordinator/XMPP Gateway
XBeeAddress64 addr64 = XBeeAddress64(COORD_MSB_ADDRESS, COORD_LSB_ADDRESS);
ZBTxRequest tx = ZBTxRequest(addr64, xbeeTxPayload, sizeof(xbeeTxPayload));
ZBTxStatusResponse txStatus = ZBTxStatusResponse();

void setup() {
//  Serial.begin(19200);  
//  // leonardo wait for serial
//  while (!Serial);

//  nss.begin(9600);
//  xbee.setSerial(nss);
  
  // WTF bug in xbee library prevents serial @ 115200 from working, ugh  
  Serial.begin(9600);
  xbee.setSerial(Serial);
}

void sendPacket() {
  // send a packet
  //Serial.println("TX");
  xbee.send(tx);
  
  // after sending a tx request, we expect a status response
  // wait up to half second for the status response
  if (xbee.readPacket(1000)) {    
    if (xbee.getResponse().getApiId() == ZB_TX_STATUS_RESPONSE) {
      //Serial.println("Got tx status");
      xbee.getResponse().getZBTxStatusResponse(txStatus);

      // get the delivery status, the fifth byte
      if (txStatus.isSuccess()) {
        //Serial.println("delivered");        
        // good
      } else {
        // bad
        //Serial.println("delivery failure");                
      }
    }      
  } else if (xbee.getResponse().isError()) {
    //Serial.println("TX error");
  }   
}

void loop() {
  xbee.readPacket();
  
  // if we get a packet, echo it back
  if (xbee.getResponse().isAvailable()) {  
    if (xbee.getResponse().getApiId() == ZB_RX_RESPONSE) {
      //Serial.println("Got rx packet");
      sendPacket();
    }      
  } else if (xbee.getResponse().isError()) {
    // todo blick lights or something
    //Serial.println("rx error");    
  }
}
