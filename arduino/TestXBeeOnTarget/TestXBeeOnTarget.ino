#include <XBee.h>

// REMEMBER TO SET BOARD TYPE MATCH THE TARGET ARDUINO (IE OPTIBOOT XXXX) WHEN COMPILING SKETCHES TO BE SENT TO SKETCHER


XBee xbee = XBee();
XBeeResponse response = XBeeResponse();
ZBRxResponse rx = ZBRxResponse();

uint8_t xbeeTxPayload[] = { 0xff };
uint32_t COORD_MSB_ADDRESS = 0x0013a200;
uint32_t COORD_LSB_ADDRESS = 0x408b98fe;

// Coordinator/XMPP Gateway
XBeeAddress64 addr64 = XBeeAddress64(COORD_MSB_ADDRESS, COORD_LSB_ADDRESS);
ZBTxRequest tx = ZBTxRequest(addr64, xbeeTxPayload, sizeof(xbeeTxPayload));
ZBTxStatusResponse txStatus = ZBTxStatusResponse();

void setup() {
  // this is going to the programmer Arduino, which relays packets to XBee on softserial
  // we're on diecimila talking to leonardo, but leonardo, which uses this serial port for flashing, confused? I am
  Serial.begin(115200);
  xbee.setSerial(Serial);
}

void loop() {
  xbee.readPacket();
    
  // send a packet
  xbee.send(tx);
  
  // after sending a tx request, we expect a status response
  // wait up to half second for the status response
  if (xbee.readPacket(1000)) {    
    if (xbee.getResponse().getApiId() == ZB_TX_STATUS_RESPONSE) {
      xbee.getResponse().getZBTxStatusResponse(txStatus);

      // get the delivery status, the fifth byte
      if (txStatus.isSuccess()) {
        // good
      } else {
        // bad
      }
    }      
  } else if (xbee.getResponse().isError()) {
      //xbee.getResponse().getErrorCode());
  } else {
    //getDebugSerial()->println("TX timeout");
  } 
  
  delay(10000);
}
