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

import org.apache.commons.cli.CommandLine;
import org.apache.commons.cli.OptionBuilder;
import org.apache.log4j.Logger;

import com.google.common.collect.Maps;
import com.rapplogic.aru.uploader.CliOptions;
import com.rapplogic.aru.uploader.serial.SerialSketchUploader;

/**
 * Uploads sketch to Nordic via the NordicSerialToSPI Sketch
 * 
 * @author andrew
 *
 */
public class NordicSketchUploader extends SerialSketchUploader {
	
	final Logger log = Logger.getLogger(NordicSketchUploader.class);
	
	private StringBuilder stringBuilder = new StringBuilder();
	
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
	
	public void processNordic(String file, String device, int speed, boolean verbose, int ackTimeout, int arduinoTimeout, int retriesPerPacket, int delayBetweenRetriesMillis) throws IOException, PortInUseException, UnsupportedCommOperationException, TooManyListenersException, StartOverException {
		Map<String,Object> context = Maps.newHashMap();
		context.put("device", device);
		context.put("speed", speed);
		
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

	public final static String serialPort = "serial-port";
	public final static String baudRate = "baud-rate";
	
	private void runFromCmdLine(String[] args) throws org.apache.commons.cli.ParseException, IOException, PortInUseException, UnsupportedCommOperationException, TooManyListenersException, StartOverException {
		CliOptions cliOptions = getCliOptions();
	
		cliOptions.getOptions().addOption(
				OptionBuilder
				.withLongOpt(serialPort)
				.hasArg()
				.isRequired(true)
				.withDescription("Serial port of of Arduino running NordicSPI2Serial sketch (e.g. /dev/tty.usbserial-A6005uRz). Required")
				.create("p"));

		cliOptions.getOptions().addOption(
				OptionBuilder
				.withLongOpt(baudRate)
				.hasArg()
				.isRequired(true)
				.withType(Number.class)
				.withDescription("Baud rate of Arduino. Required")
				.create("b"));
		
		CommandLine commandLine = cliOptions.parse(args);

		if (commandLine != null) {
			boolean verbose = false;
			
			if (commandLine.hasOption(CliOptions.verboseArg)) {
				verbose = true;
			}
			
			//public void process(String file, int pageSize, final int ackTimeoutMillis, int arduinoTimeoutSec, int retriesPerPacket, int delayBetweenRetriesMillis, final boolean verbose, final Map<String,Object> context) throws IOException {
			//????public void process(String file, String device, int speed, String nordicAddress, int ackTimeout, int arduinoTimeout, int retriesPerPacket, int delayBetweenRetriesMillis, boolean verbose, int timeout) throws IOException, PortInUseException, UnsupportedCommOperationException, TooManyListenersException, StartOverException {
			// cmd line
			new NordicSketchUploader().processNordic(
					commandLine.getOptionValue(CliOptions.sketch), 
					commandLine.getOptionValue(serialPort), 
					cliOptions.getIntegerOption(baudRate), 
					verbose,
					cliOptions.getIntegerOption(CliOptions.ackTimeoutMillisArg),
					cliOptions.getIntegerOption(CliOptions.arduinoTimeoutArg),
					cliOptions.getIntegerOption(CliOptions.retriesPerPacketArg),
					cliOptions.getIntegerOption(CliOptions.delayBetweenRetriesMillisArg));
		}
	}
	
	/**
	 * ex ceylon:arduino-remote-uploader-1.0-SNAPSHOT andrew$ ./nordic-uploader.sh --sketch /Users/andrew/Documents/dev/arduino-remote-uploader/resources/BlinkSlow.cpp.hex --serial-port /dev/tty.usbmodemfa131 --baud-rate 19200 arduino-timeout 0 --ack-timeout-ms 5000 --retries 50
Experimental:  JNI_OnLoad called.
Stable Library
=========================================
Native lib Version = RXTX-2.1-7
Java lib Version   = RXTX-2.1-7
Sending sketch to nRF24L01 radio, size 1102 bytes, md5 8e7a58576bdc732d3f9708dab9aea5b9, number of packets 43, and 26 bytes per packet, header ef,ac,10,a,4,4e,0,2b,1a,3c
...........................................
Successfully flashed remote Arduino in 3s, with 0 retries

	 * @param args
	 * @throws NumberFormatException
	 * @throws IOException
	 * @throws ParseException
	 * @throws org.apache.commons.cli.ParseException
	 * @throws PortInUseException
	 * @throws UnsupportedCommOperationException
	 * @throws TooManyListenersException
	 * @throws StartOverException
	 */
	public static void main(String[] args) throws NumberFormatException, IOException, ParseException, org.apache.commons.cli.ParseException, PortInUseException, UnsupportedCommOperationException, TooManyListenersException, StartOverException {		
		initLog4j();
		new NordicSketchUploader().runFromCmdLine(args);
//		new NordicSketchUploader().processNordic("/Users/andrew/Documents/dev/arduino-remote-uploader/resources/RAU-328-13k.hex", "/dev/tty.usbmodemfa131", Integer.parseInt("19200"), false, 5, 0, 50, 250);
	}
}