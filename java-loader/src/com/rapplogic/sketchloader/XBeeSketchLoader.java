package com.rapplogic.sketchloader;

import java.io.IOException;

import com.rapplogic.xbee.api.XBee;
import com.rapplogic.xbee.api.XBeeAddress64;
import com.rapplogic.xbee.api.XBeeException;
import com.rapplogic.xbee.api.zigbee.ZNetTxRequest;

public class XBeeSketchLoader extends ArduinoSketchLoader {

	public XBeeSketchLoader() {
		super();
	}
	
	final int PAGE_SIZE = 80;
	
	public void process(String file, String device, int speed) throws IOException {
		// page size is max packet size for the radio
		Sketch sketch = getSketch(file, PAGE_SIZE);
		
		XBee xbee = new XBee();
		
		try {
			xbee.open(device, speed);
			
			XBeeAddress64 xBeeAddress64 = new XBeeAddress64(new int[] { 1, 2, 3});
			
			// TODO send request to start programming and wait for reply
			// TODO more robust approach is to send async then wait for rx acknowledgement
			// TODO put a magic word in each packet to differentiate from other radios that might be trying to communicate during proramming. for now we'll say unsupported?
		
			// send header size + #pages
			xbee.sendSynchronous(new ZNetTxRequest(xBeeAddress64, new int[] { sketch.getSize() >> 8, sketch.getSize() & 0xff, sketch.getPages().size() }));
			
			for (Page page : sketch.getPages()) {
				// send to radio, one page at a time
				xbee.sendSynchronous(new ZNetTxRequest(xBeeAddress64, page.getData()));
			}
			
			// TODO wait for rx packet to indicate success or failure			
		} catch (Exception e) {
			try {
				xbee.close();
			} catch (Exception e2) {}
		}
	}

	public static void main(String[] args) throws NumberFormatException, IOException, XBeeException {
		// sketch hex file, device, speed, xbee address, radio_type  
		new XBeeSketchLoader().process(args[0], args[1], Integer.parseInt(args[2]));
	}
}