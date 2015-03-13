/**
 * Copyright (c) 2015 Andrew Rapp. All rights reserved.
 *
 * This file is part of arduino-remote-uploader
 *
 * arduino-remote-uploader is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * arduino-remote-uploader is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with arduino-remote-uploader.  If not, see <http://www.gnu.org/licenses/>.
 */

package com.rapplogic.aru.uploader.nordic;

import gnu.io.PortInUseException;
import gnu.io.SerialPortEvent;
import gnu.io.UnsupportedCommOperationException;

import java.io.IOException;
import java.text.ParseException;
import java.util.TooManyListenersException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.ReentrantLock;

import org.apache.log4j.Logger;

import com.rapplogic.aru.core.Page;
import com.rapplogic.aru.core.Sketch;
import com.rapplogic.aru.uploader.serial.SerialSketchLoader;
import com.rapplogic.xbee.api.XBeeException;

/**
 * 
 * @author andrew
 *
 */
public class NordicSketchLoader extends SerialSketchLoader {
	
	final Logger log = Logger.getLogger(NordicSketchLoader.class);
	
	final ReentrantLock lock = new ReentrantLock();
	final Condition replyCondition = lock.newCondition();
	
	private StringBuilder stringBuilder = new StringBuilder();
	
	private final int NORDIC_PAGE_SIZE = 26;
	
	public NordicSketchLoader() {
		super();
	}

	protected void handleSerial(SerialPortEvent event) {
       switch (event.getEventType()) {
           case SerialPortEvent.DATA_AVAILABLE:
               byte[] readBuffer = new byte[32];
               
               try {
            	   int numBytes = getInputStream().read(readBuffer);
            	   
            	   for (int i = 0; i < numBytes; i++) {
            		   //System.out.println("read " + (char) readBuffer[i]);
 
            		   // don't add lf/cr chars
            		   if ((readBuffer[i] != 10 && readBuffer[i] != 13)) {
            			   stringBuilder.append((char) readBuffer[i]);                            
            		   }

            		   // got a new line
            		   if ((int)readBuffer[i] == 10) {    
            			   handleSerialReply(stringBuilder.toString());
            			   stringBuilder = new StringBuilder();
            		   }
            	   }
               } catch (Exception e) {
            	   log.error("Serial error", e);
               }

               break;
           }
	}
	
	protected void handleSerialReply(String reply) throws InterruptedException {
		lock.lockInterruptibly();
		
		try {
			System.out.println("<-" + reply);
			this.reply = reply;
			replyCondition.signal();
		} finally {
			lock.unlock();
		}
	}
	
	String reply = null;
	
	private void waitForAck(final int timeout) throws InterruptedException {	
		lock.lockInterruptibly();
		
		try {
			// keep waiting until we get ok or error, rest is debug
			while (replyCondition.await(timeout, TimeUnit.SECONDS)) {				
				if (reply.startsWith("OK")) {
					// ok
					return;
				} else if (reply.startsWith("ERROR")) {
					throw new RuntimeException(reply);
				}
			}
			
			throw new RuntimeException("Timeout");
		} finally {
			lock.unlock();
		}
	}

	private void writePacket(int[] data) throws IOException {
		//System.out.println("Writing packet " + toHex(data));
		
		for (int i = 0; i < data.length; i++) {
			write(data[i]);
		}		
	}

	// TODO put into superclass
	public void process(String file, String device, int speed, String nordicAddress, boolean verbose, int timeout) throws IOException, PortInUseException, UnsupportedCommOperationException, TooManyListenersException {
		// page size is max packet size for the radio
		Sketch sketch = parseSketchFromIntelHex(file, NORDIC_PAGE_SIZE);
				
		try {
			this.openSerial(device, 19200);
			
			long start = System.currentTimeMillis();
			
			System.out.println("Sending sketch to nordic radio via Serial2SPI sketch, size " + sketch.getSize() + " bytes, md5 " + getMd5(sketch.getProgram()) + ", number of packets " + sketch.getPages().size() + ", and " + sketch.getBytesPerPage() + " bytes per packet, header " + toHex(getStartHeader(sketch.getSize(), sketch.getPages().size(), sketch.getBytesPerPage())));			

			writePacket(getStartHeader(sketch.getSize(), sketch.getPages().size(), sketch.getBytesPerPage()));
			
			waitForAck(timeout);
			
			for (Page page : sketch.getPages()) {				
				// make sure we exit on a kill signal like a good app
				if (Thread.currentThread().isInterrupted()) {
					throw new InterruptedException();
				}
				
				int[] data = combine(getEEPROMWriteHeader(page.getRealAddress16(), page.getData().length), page.getData());
				
				if (verbose) {
					System.out.println("Sending page " + page.getOrdinal() + " of " + sketch.getPages().size() + ", with address " + page.getRealAddress16() + ", length " + data.length + ", packet " + toHex(data));
//					System.out.println("Data " + toHex(page.getData()));
				} else {
					System.out.print(".");
				}

				writePacket(data);
				
				// don't send next page until this one is processed or we will overflow the buffer
				waitForAck(timeout);
			}

			if (!verbose) {
				System.out.println("");
			}

			System.out.println("Sending flash start packet " + toHex(getFlashStartHeader(sketch.getSize())));
			
			writePacket(getFlashStartHeader(sketch.getSize()));
			
			waitForAck(timeout);
			
			System.out.println("Successfully flashed remote Arduino in " + (System.currentTimeMillis() - start) + "ms");
		} catch (InterruptedException e) {
			// kill signal
			System.out.println("Interrupted during programming.. exiting");
			return;
		} catch (Exception e) {
			log.error("Unexpected error", e);
		} finally {
			try {
				getSerialPort().close();
			} catch (Exception e) {}
		}
	}
	
	public static void main(String[] args) throws NumberFormatException, IOException, XBeeException, ParseException, org.apache.commons.cli.ParseException, PortInUseException, UnsupportedCommOperationException, TooManyListenersException {		
		initLog4j();
		
//		if (false) {
//			runFromCmdLine(args);
//		} else {
			// run from eclipse for dev
//			new NordicSketchLoader().process("/Users/andrew/Documents/dev/arduino-remote-uploader/resources/BlinkSlow.cpp.hex", "/dev/tty.usbmodemfa131", Integer.parseInt("19200"), "????", true, 5);
			new NordicSketchLoader().process("/Users/andrew/Documents/dev/arduino-remote-uploader/resources/BlinkFast.cpp.hex", "/dev/tty.usbmodemfa131", Integer.parseInt("19200"), "????", true, 5);
//		}
	}
}