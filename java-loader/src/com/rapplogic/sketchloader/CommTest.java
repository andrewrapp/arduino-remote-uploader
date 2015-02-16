package com.rapplogic.sketchloader;

import java.io.IOException;

import com.rapplogic.xbee.api.PacketListener;
import com.rapplogic.xbee.api.XBee;
import com.rapplogic.xbee.api.XBeeAddress64;
import com.rapplogic.xbee.api.XBeeException;
import com.rapplogic.xbee.api.XBeeResponse;
import com.rapplogic.xbee.api.XBeeTimeoutException;
import com.rapplogic.xbee.api.zigbee.ZNetTxRequest;
import com.rapplogic.xbee.api.zigbee.ZNetTxStatusResponse;

public class CommTest {

	public CommTest() {

	}

	//Received RX packet apiId=ZNET_RX_RESPONSE (0x90),length=13,checksum=0x12,error=false,remoteAddress64=0x00,0x13,0xa2,0x00,0x40,0x8b,0x98,0xff,remoteAddress16=0x60,0xdd,option=PACKET_ACKNOWLEDGED,data=0x08
//	7E,0,10,90,0,7D,33,A2,0,40,8B,98,FE,0,0,1,42,4F,4F,4D,2B
//	7E,0,10,90,0,7D,33,A2,0,40,8B,98,FE,0,0,1,42,4F,4F,4D,2B,	

	// configuration 2 radios for this test a coordinator and a router. With series2 you need one coordinator and there can be only one coordinator per network (pan id)
	
	public void test() throws XBeeTimeoutException, XBeeException {
		XBee xbee = new XBee();
		
		// yellow star
		xbee.open("/dev/tty.usbserial-A6005uRz", 9600);

		xbee.addPacketListener(new PacketListener() {
			
			@Override
			public void processResponse(XBeeResponse response) {
				System.out.println("Received RX packet " + response);
			}
		});
		
		// green star
		XBeeAddress64 addr64 = new XBeeAddress64("0013A200408B98FF");
//		XBeeAddress64 addr64 = new XBeeAddress64(0, 0x13, 0xa2, 0, 0x40, 0x8b, 0x98, 0xff);
		 
		// create an array of arbitrary data to send
		int[] payload = new int[] { 'B', 'O', 'O', 'M' };
		
		// first request we just send 64-bit address.  we get 16-bit network address with status response
		ZNetTxRequest request = new ZNetTxRequest(addr64, payload);
		
		while (true) {			
			System.out.println("Sending tx " + request);
			ZNetTxStatusResponse response = (ZNetTxStatusResponse) xbee.sendSynchronous(request);
			System.out.println("Response " + response);
			
			try {
				Thread.sleep(10000);
			} catch (InterruptedException e) {
				break;
			}
		}		
	}
	public static void main(String[] args) throws NumberFormatException, IOException, XBeeException {
		// sketch hex file, device, speed, xbee address, radio_type  
		new CommTest().test();
	}
}