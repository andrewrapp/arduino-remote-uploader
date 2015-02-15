package com.rapplogic.sketchloader;

import java.io.IOException;

import com.rapplogic.xbee.api.XBee;
import com.rapplogic.xbee.api.XBeeAddress64;
import com.rapplogic.xbee.api.XBeeConfiguration;
import com.rapplogic.xbee.api.XBeeException;
import com.rapplogic.xbee.api.XBeeTimeoutException;
import com.rapplogic.xbee.api.zigbee.ZNetTxRequest;
import com.rapplogic.xbee.api.zigbee.ZNetTxStatusResponse;

public class CommTest {

	public CommTest() {

	}

	// configuration 2 radios for this test a coordinator and a router. With series2 you need one coordinator and there can be only one coordinator per network (pan id)
	
	public void test() throws XBeeTimeoutException, XBeeException {
		XBee xbee = new XBee();
	
		// yellow star
		xbee.open("/dev/tty.usbserial-A6005uRz", 9600);
		
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