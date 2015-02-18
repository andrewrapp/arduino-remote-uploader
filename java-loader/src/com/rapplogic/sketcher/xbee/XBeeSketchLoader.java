package com.rapplogic.sketcher.xbee;

import java.io.IOException;
import java.util.Arrays;
import java.util.List;

import com.google.common.collect.Lists;
import com.rapplogic.sketcher.SketchLoaderCore;
import com.rapplogic.sketcher.Page;
import com.rapplogic.sketcher.Sketch;
import com.rapplogic.xbee.api.ApiId;
import com.rapplogic.xbee.api.PacketListener;
import com.rapplogic.xbee.api.XBee;
import com.rapplogic.xbee.api.XBeeAddress64;
import com.rapplogic.xbee.api.XBeeException;
import com.rapplogic.xbee.api.XBeeResponse;
import com.rapplogic.xbee.api.zigbee.ZNetRxResponse;
import com.rapplogic.xbee.api.zigbee.ZNetTxRequest;
import com.rapplogic.xbee.api.zigbee.ZNetTxStatusResponse;
import com.rapplogic.xbee.api.zigbee.ZNetTxStatusResponse.DeliveryStatus;

public class XBeeSketchLoader extends SketchLoaderCore {

	public XBeeSketchLoader() {
		super();
	}
	
	// block size for eeprom writes
	final int PAGE_SIZE = 64;
	final int MAGIC_BYTE1 = 0xef;
	final int MAGIC_BYTE2 = 0xac;
	// make enum
	final int CONTROL_PROG_REQUEST = 0x10; 	//10000
	final int CONTROL_WRITE_EEPROM = 0x20; 	//100000
	// somewhat redundant
	final int CONTROL_START_FLASH = 0x40; 	//1000000
	
	final int OK = 1;
	final int FAILURE = 2;
	// too much time has passed between receiving packets
	final int TIMEOUT = 3;
	// xbee just woke, send programming now!
	final int WAKE = 4;
	
	final Object lock = new Object();
	
	private int[] getStartHeader(int sizeInBytes, int numPages, int bytesPerPage) {
		return new int[] { 
				MAGIC_BYTE1, 
				MAGIC_BYTE2, 
				CONTROL_PROG_REQUEST, 
				(sizeInBytes >> 8) & 0xff, 
				sizeInBytes & 0xff, 
				(numPages >> 8) & 0xff, 
				numPages & 0xff,
				bytesPerPage
		};
	}
	
	// xbee has error detection built-in but other protocols may need a checksum
	private int[] getHeader(int controlByte, int sixteenBitValue) {
		return new int[] {
				MAGIC_BYTE1, 
				MAGIC_BYTE2, 
				controlByte, 
				(sixteenBitValue >> 8) & 0xff, 
				sixteenBitValue & 0xff
		};
	}

	private int[] getEEPROMWriteHeader(int address16) {
		return getHeader(CONTROL_WRITE_EEPROM, address16);
	}
	
	private int[] getFlashStartHeader(int progSize) {
		return getHeader(CONTROL_START_FLASH, progSize);
	}	
	
	private void waitForAck() throws InterruptedException {
		synchronized (lock) {
			long now = System.currentTimeMillis();
			
			lock.wait(10000);
			
			if (System.currentTimeMillis() - now >= 10000) {
				throw new RuntimeException("Timeout");
			}
			
			if (messages.get(0).intValue() == OK) {
//				System.out.println("Got ACK");
			} else {
				throw new RuntimeException("Sketch failed");
			}
		}
	}

	final List<Integer> messages = Lists.newArrayList();
	
	public void process(String file, String device, int speed, String xbeeAddress) throws IOException {
		// page size is max packet size for the radio
		Sketch sketch = parseSketchFromIntelHex(file, PAGE_SIZE);
		
		XBee xbee = new XBee();
		
		try {
			xbee.open(device, speed);
			
			xbee.addPacketListener(new PacketListener() {
				@Override
				public void processResponse(XBeeResponse response) {
					//System.out.println("Received message from sketcher " + response);
					
					if (response.getApiId() == ApiId.ZNET_RX_RESPONSE) {
						ZNetRxResponse zb = (ZNetRxResponse) response;
						messages.clear();
						
						// TODO use magic packet. this is weak
						if (zb.getData()[0] == OK || zb.getData()[0] == FAILURE) {
							messages.add(zb.getData()[0]);
							
							synchronized (lock) {
								lock.notify();
							}							
						} else {
							System.out.println("Ignoring non-programming packet " + zb);
						}
					}
				}
			});
			
			if (sketch.getSize() != sketch.getLastPage().getRealAddress16() + sketch.getLastPage().getData().length) {
				throw new RuntimeException("boom");
			}
			
			XBeeAddress64 xBeeAddress64 = new XBeeAddress64(xbeeAddress);
			
			// TODO send request to start programming and wait for reply
			// TODO more robust approach is to send async then wait for rx acknowledgement
			// TODO put a magic word in each packet to differentiate from other radios that might be trying to communicate during proramming. for now we'll say unsupported?
			// TODO consider sending version number, a weak hash of hex file so we can query what version is on the device. could simply add up the bytes and send as 24-bit value
			
			long start = System.currentTimeMillis();
			
			// send header:  size + #pages
			System.out.println("Sending sketch to xbee radio with address " + xBeeAddress64.toString() + ", size " + sketch.getSize() + " bytes, number of packets " + sketch.getPages().size() + ", and " + sketch.getBytesPerPage() + " bytes per packet");			
			ZNetTxStatusResponse response = (ZNetTxStatusResponse) xbee.sendSynchronous(new ZNetTxRequest(xBeeAddress64, getStartHeader(sketch.getSize(), sketch.getPages().size(), sketch.getBytesPerPage())));			
			
			if (response.isError() || response.getDeliveryStatus() != DeliveryStatus.SUCCESS) {
				throw new RuntimeException("Failed to deliver programming start packet " + response);
			}
			
			waitForAck();
			
			for (Page page : sketch.getPages()) {
				// send to radio, one page at a time
				// TODO handle errors and retries
				// send address since so we know where to write this in the eeprom
				
				int[] data = combine(getEEPROMWriteHeader(page.getRealAddress16()), page.getData());
				System.out.println("Sending page " + page.getOrdinal() + " of " + sketch.getPages().size() + ", with address " + page.getRealAddress16() + ", packet " + toHex(data));
//				System.out.println("Data " + toHex(page.getData()));
				
				response = (ZNetTxStatusResponse) xbee.sendSynchronous(new ZNetTxRequest(xBeeAddress64, data));
				
				if (response.isSuccess() || response.getDeliveryStatus() != DeliveryStatus.SUCCESS) {
//					System.out.print("#");					
				} else {
					throw new RuntimeException("Failed to deliver packet at page " + page.getOrdinal() + " of " + sketch.getPages().size() + ", response " + response);
				}
				
				// don't send next page until this one is processed or we will overflow the buffer
				waitForAck();
			}

			System.out.println("Sending flash start packet " + toHex(getFlashStartHeader(sketch.getSize())));
			response = (ZNetTxStatusResponse) xbee.sendSynchronous(new ZNetTxRequest(xBeeAddress64, getFlashStartHeader(sketch.getSize())));

			if (response.isError() || response.getDeliveryStatus() != DeliveryStatus.SUCCESS) {
				throw new RuntimeException("Flash start packet failed to deliver " + response);					
			}
			
			waitForAck();
			
			System.out.println("Successfully flashed remote Arduino in " + (System.currentTimeMillis() - start) + "ms");

			xbee.close();
		} catch (Exception e) {
			e.printStackTrace();
			try {
				xbee.close();
			} catch (Exception e2) {}
		}
	}
	
	private int[] combine(int[] a, int[] b) {
		int[] result = Arrays.copyOf(a, a.length + b.length);
		System.arraycopy(b, 0, result, a.length, b.length);
		return result;
	}

	public static void main(String[] args) throws NumberFormatException, IOException, XBeeException {
		// sketch hex file, device, speed, xbee address, radio_type  
		//new XBeeSketchLoader().process(args[0], args[1], Integer.parseInt(args[2]), args[3]);
		//new XBeeSketchLoader().process("/Users/andrew/Documents/dev/arduino-sketch-loader/resources/HelloTest.cpp.hex", "/dev/tty.usbserial-A6005uRz", Integer.parseInt("9600"), "0013A200408B98FF");
		//new XBeeSketchLoader().process("/Users/andrew/Documents/dev/arduino-sketch-loader/resources/TestXBeeOnTarget.cpp.hex", "/dev/tty.usbserial-A6005uRz", Integer.parseInt("9600"), "0013A200408B98FF");
		new XBeeSketchLoader().process("/Users/andrew/Documents/dev/arduino-sketch-loader/resources/XBeeEcho.cpp.hex", "/dev/tty.usbserial-A6005uRz", Integer.parseInt("9600"), "0013A200408B98FF");
	}
}