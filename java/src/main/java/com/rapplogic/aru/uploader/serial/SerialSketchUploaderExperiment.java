package com.rapplogic.aru.uploader.serial;


import gnu.io.SerialPort;
import gnu.io.SerialPortEventListener;

import java.io.InputStream;

import com.rapplogic.aru.core.Page;
import com.rapplogic.aru.core.Sketch;

/**
 * Loads a sketch from host filesystem onto an Arduino via an Arduino Leonardo, running the SerialSketcher sketch
 * 
 * @author andrew
 *
 */
public abstract class SerialSketchUploaderExperiment extends SerialSketchUploader implements SerialPortEventListener {

	final int FIRST_PAGE = 0xd;
	final int LAST_PAGE = 0xf;
	final int PAGE_DATA = 0xa;
	
	//final int BAUD_RATE = 115200;
	// usb-serial speed. needs to match the sketch of course
	final int BAUD_RATE = 19200;
	
	private InputStream inputStream;
	private SerialPort serialPort;
    private Object pageAck = new Object();
    private StringBuffer strBuf = new StringBuffer();
    
	public SerialSketchUploaderExperiment() {

	}
	
	public void process(String device, String hex) throws Exception {

		int[] program = parseIntelHex(hex);
		Sketch sketch = parseSketchFromIntelHex(hex, ARDUINO_PAGE_SIZE);	
		
		//System.out.println("Sending sketch to Arduino via serial. Program length is " + program.length + ", there are " + sketch.getPages().size() + " pages");
		
		this.openSerial(device, BAUD_RATE);
		
		for (int i = 0; i < sketch.getPages().size(); i++) {
			Page page = sketch.getPages().get(i);
			
			System.out.println("Sending page " + (i + 1) + " of " + sketch.getPages().size() + ", length is " + page.getData().length + ", address is " + Integer.toHexString(page.getBootloaderAddress16()) + ", page is " + toHex(page.getPage()));

			if (i == 0) {
				write(FIRST_PAGE);
			} else if (i == sketch.getPages().size() - 1) {
				write(LAST_PAGE);
			} else {
				write(PAGE_DATA);
			}
		
//			// only data length, does not include ctrl, len, or addr bytes
			write(page.getData().length);
			
			for (int k = 0; k < page.getPage().length; k++) {
				write(page.getPage()[k] & 0xff);
			}
			
			serialPort.getOutputStream().flush();
			
//			System.out.println("Waiting for ack from Arduino");
			
//			// wait for reply before sending more data
			synchronized (pageAck) {
				// TODO timeout
				pageAck.wait();
			}
		}
		
		System.out.println("Java done");
		
		// wait a few secs for leave prog mode reply
		Thread.sleep(5000);
		
		// close port to trigger IOException on blocking read call and exit jvm
		serialPort.close();
	}
	
	public SerialPort getSerialPort() {
		return serialPort;
	}
}
