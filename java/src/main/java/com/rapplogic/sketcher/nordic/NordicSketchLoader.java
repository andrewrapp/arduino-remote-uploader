/**
 * Copyright (c) 2015 Andrew Rapp. All rights reserved.
 *
 * This file is part of arduino-sketcher
 *
 * arduino-sketcher is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * arduino-sketcher is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with arduino-sketcher.  If not, see <http://www.gnu.org/licenses/>.
 */

package com.rapplogic.sketcher.nordic;

import gnu.io.PortInUseException;
import gnu.io.SerialPortEvent;
import gnu.io.UnsupportedCommOperationException;

import java.io.IOException;
import java.text.ParseException;
import java.util.List;
import java.util.TooManyListenersException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.ReentrantLock;

import org.apache.commons.cli.CommandLine;
import org.apache.commons.cli.CommandLineParser;
import org.apache.commons.cli.Options;
import org.apache.commons.cli.PosixParser;

import com.google.common.collect.Lists;
import com.rapplogic.sketcher.Page;
import com.rapplogic.sketcher.Sketch;
import com.rapplogic.sketcher.serial.SerialSketchLoader;
import com.rapplogic.xbee.api.XBeeException;

/**
 * 
 * @author andrew
 *
 */
public class NordicSketchLoader extends SerialSketchLoader {
	
	final List<Integer> messages = Lists.newArrayList();
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
               // we get here if data has been received
               byte[] readBuffer = new byte[20];
               
               try {
                   // read data
            	   int numBytes = getInputStream().read(readBuffer);
            	   
            	   for (int i = 0; i < numBytes; i++) {
            		   //System.out.println("read " + (char) readBuffer[i]);
 
            		   if ((readBuffer[i] != 10 && readBuffer[i] != 13)) {
            			   stringBuilder.append((char) readBuffer[i]);                            
            		   }

            		   // line
            		   if ((int)readBuffer[i] == 10) {    
            			   handleSerialReply(stringBuilder.toString());
            			   stringBuilder = new StringBuilder();
            		   }
            	   }
               } catch (Exception e) {
                   throw new RuntimeException("serialEvent error ", e);
               }

               break;
           }
	}
	
	protected void handleSerialReply(String reply) throws InterruptedException {
		lock.lockInterruptibly();
		
		try {
			System.out.println("Arduino<-" + reply);
			this.reply = reply;
			replyCondition.signal();
		} finally {
			lock.unlock();
		}
	}
	
	String reply = null;
	
	private void waitForAck(final int timeout) throws InterruptedException {
		long now = System.currentTimeMillis();
		// TODO send this timeout to arduino in prog start header
		
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
		System.out.println("Writing packet " + toHex(data));
		
		for (int i = 0; i < data.length; i++) {
			write(data[i]);
		}		
	}

	public void process(String file, String device, int speed, String nordicAddress, boolean verbose, int timeout) throws IOException, PortInUseException, UnsupportedCommOperationException, TooManyListenersException {
		// page size is max packet size for the radio
		Sketch sketch = parseSketchFromIntelHex(file, NORDIC_PAGE_SIZE);
				
		try {
			this.openSerial(device, 19200);
			
			long start = System.currentTimeMillis();
			
			System.out.println("Sending sketch to nordic radio, size " + sketch.getSize() + " bytes, md5 " + getMd5(sketch.getProgram()) + ", number of packets " + sketch.getPages().size() + ", and " + sketch.getBytesPerPage() + " bytes per packet");			

			writePacket(getStartHeader(sketch.getSize(), sketch.getPages().size(), sketch.getBytesPerPage()));
			
			waitForAck(timeout);
			
			for (Page page : sketch.getPages()) {				
				// make sure we exit on a kill signal like a good app
				if (Thread.currentThread().isInterrupted()) {
					throw new InterruptedException();
				}
				
				int[] data = combine(getEEPROMWriteHeader(page.getRealAddress16(), page.getData().length), page.getData());
				
				if (verbose) {
					System.out.println("Sending page " + page.getOrdinal() + " of " + sketch.getPages().size() + ", with address " + page.getRealAddress16() + ",  length " + data.length + ", packet " + toHex(data));
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
			//psize 44E (1102),cur addr 45E 1118.. off by 16

			System.out.println("Sending flash start packet " + toHex(getFlashStartHeader(sketch.getSize())));
			
			writePacket(getFlashStartHeader(sketch.getSize()));
			
			waitForAck(timeout);
			
			System.out.println("Successfully flashed remote Arduino in " + (System.currentTimeMillis() - start) + "ms");

			getSerialPort().close();
		} catch (InterruptedException e) {
			// kill signal
			System.out.println("Interrupted during programming.. exiting");
			return;
		} catch (Exception e) {
			// TODO log
			e.printStackTrace();
		}
	}
	
	public static void main(String[] args) throws NumberFormatException, IOException, XBeeException, ParseException, org.apache.commons.cli.ParseException, PortInUseException, UnsupportedCommOperationException, TooManyListenersException {		
		initLog4j();
		
//		if (false) {
//			runFromCmdLine(args);
//		} else {
			// run from eclipse for dev
			//new NordicSketchLoader().process("/Users/andrew/Documents/dev/arduino-sketcher/resources/BlinkSlow.cpp.hex", "/dev/tty.usbmodemfa131", Integer.parseInt("19200"), "????", true, 5);
			new NordicSketchLoader().process("/Users/andrew/Documents/dev/arduino-sketcher/resources/BlinkFast.cpp.hex", "/dev/tty.usbmodemfa131", Integer.parseInt("19200"), "????", true, 5);
//		}
	}
}