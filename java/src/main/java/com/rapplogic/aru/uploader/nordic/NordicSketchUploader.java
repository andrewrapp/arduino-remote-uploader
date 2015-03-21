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
import java.util.Map;
import java.util.TooManyListenersException;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.ReentrantLock;

import org.apache.log4j.Logger;

import com.google.common.collect.Maps;
import com.rapplogic.aru.uploader.serial.SerialSketchUploader;
import com.rapplogic.xbee.api.XBeeException;

/**
 * Uploads sketch to Nordic via the NordicSerialToSPI Sketch
 * 
 * @author andrew
 *
 */
public class NordicSketchUploader extends SerialSketchUploader {
	
	final Logger log = Logger.getLogger(NordicSketchUploader.class);
	
	final ReentrantLock lock = new ReentrantLock();
	final Condition replyCondition = lock.newCondition();
	
	private StringBuilder stringBuilder = new StringBuilder();
	
	private String reply = null;
	
	private final int NORDIC_PACKET_SIZE = 32;
	
	public NordicSketchUploader() {
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
		// convert back to byte array for framework
		int[] replyArray = new int[] { MAGIC_BYTE1, MAGIC_BYTE2, 0, 0, 0};

		if (isVerbose()) {
			System.out.println("<-" + reply);				
		}
		
		// because we only have one serial port I've adopted a simple convention to use the serial
		// port for both debugging and control. Here's the convention:
		// starts with OK continue with net
		// starts with RETRY tx err/no ack, try again
		// starts with ERROR unrecoverable, fail
		// anything else is just debug
		
		if (reply.startsWith("OK")) {
			// parse id
			int replyId = Integer.parseInt(reply.split(",")[1].trim());
			
			replyArray[2] = OK;
			replyArray[3] = (replyId >> 8) & 0xff;
			replyArray[4] = replyId & 0xff;
		} else if (reply.startsWith("RETRY")) {
			// hacky
			replyArray[2] =	RETRY;
		} else if (reply.startsWith("ERROR")) {
			replyArray[2] = START_OVER;
		}
		
		// if not debug message, add reply to queue
		if (replyArray[2] != 0) {
			addReply(replyArray);
		}
	}
	
	@Override
	public void open(Map<String,Object> context) throws Exception {
		this.openSerial((String) context.get("device"), (Integer) context.get("speed"));
	}

	@Override
	public void close() throws Exception {
		getSerialPort().close();
	}
	
	// TODO send nordic address
	
	public void process(String file, String device, int speed, String nordicAddress, int ackTimeout, int arduinoTimeout, int retriesPerPacket, int delayBetweenRetriesMillis, boolean verbose, int timeout) throws IOException, PortInUseException, UnsupportedCommOperationException, TooManyListenersException, StartOverException {
		Map<String,Object> context = Maps.newHashMap();
		context.put("device", device);
		context.put("speed", speed);
		context.put("nordicAddress", nordicAddress);
		
		// determine max data we can send with each programming packet
		int pageSize = NORDIC_PACKET_SIZE - getProgramPageHeader(0, 0).length;
		super.process(file, pageSize, ackTimeout, arduinoTimeout, retriesPerPacket, delayBetweenRetriesMillis, verbose, context);
	}

	@Override
	protected void writeData(int[] data, Map<String, Object> context) throws Exception {
		//System.out.println("Writing packet " + toHex(data));
		for (int i = 0; i < data.length; i++) {
			write(data[i]);
		}
	}

	@Override
	protected String getName() {
		return "nRF24L01";
	}
	
	public static void main(String[] args) throws NumberFormatException, IOException, XBeeException, ParseException, org.apache.commons.cli.ParseException, PortInUseException, UnsupportedCommOperationException, TooManyListenersException, StartOverException {		
		initLog4j();
		
//		if (false) {
//			runFromCmdLine(args);
//		} else {
			// run from eclipse for dev
//			new NordicSketchLoader().process("/Users/andrew/Documents/dev/arduino-remote-uploader/resources/BlinkSlow.cpp.hex", "/dev/tty.usbmodemfa131", Integer.parseInt("19200"), "????", 5, 60, 10, 250, true, 5);
//			new NordicSketchUploader().process("/Users/andrew/Documents/dev/arduino-remote-uploader/resources/BlinkFast.cpp.hex", "/dev/tty.usbmodemfa131", Integer.parseInt("19200"), "????", 5, 0, 10, 250, true, 5);
			new NordicSketchUploader().process("/Users/andrew/Documents/dev/arduino-remote-uploader/resources/RAU-328-13k.hex", "/dev/tty.usbmodemfa131", Integer.parseInt("19200"), "????", 5, 0, 50, 250, true, 5);
//		}
	}
}