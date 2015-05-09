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

package com.rapplogic.aru.uploader.xbee;

import java.io.IOException;
import java.text.ParseException;
import java.util.Map;
import java.util.Queue;

import org.apache.commons.cli.CommandLine;
import org.apache.commons.cli.OptionBuilder;

import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.rapplogic.aru.uploader.CliOptions;
import com.rapplogic.aru.uploader.SketchUploader;
import com.rapplogic.xbee.api.ApiId;
import com.rapplogic.xbee.api.PacketListener;
import com.rapplogic.xbee.api.XBee;
import com.rapplogic.xbee.api.XBeeAddress64;
import com.rapplogic.xbee.api.XBeeException;
import com.rapplogic.xbee.api.XBeeResponse;
import com.rapplogic.xbee.api.XBeeTimeoutException;
import com.rapplogic.xbee.api.zigbee.ZNetRxResponse;
import com.rapplogic.xbee.api.zigbee.ZNetTxRequest;
import com.rapplogic.xbee.api.zigbee.ZNetTxStatusResponse;

/**
 * Only tested on Mac. I've included RXTX libraries for windows, and linux, so should work for 32-bit jvms
 * On mac you must use the java 1.6 that comes with Mac as Oracle Java for Mac does not support 32-bit mode, which RXTX requires (RXTX seems to be abandoned and they don't release 64-bit binaries)
 * Similarly on other platforms that don't support 32-bit mode, you'll need to find a java version that does
 *  
 * ex ./sketch-loader.sh --sketch_hex /var/folders/g1/vflh_srj3gb8zvpx_r5b9phw0000gn/T/build1410674702632504781.tmp/Blink.cpp.hex --serial_port /dev/tty.usbserial-A6005uRz --baud_rate 9600 --remote_xbee_address 0013A200408B98FF
 * 
 * @author andrew
 *
 */
public class XBeeSketchUploader extends SketchUploader {

	// TODO xbee just woke, send programming!
	//final int WAKE = 4;
	
	// block size for eeprom writes
	// TODO make this configurable. series 1 radios can support a larger payload
	public final int XBEE_PAGE_SIZE = 64;

	private final XBee xbee = new XBee();
	
	public XBeeSketchUploader() {
		super();
	}

	public void processXBee(String file, String device, int speed, String xbeeAddress, final boolean verbose, int ackTimeoutMillis, int arduinoTimeoutSec, int retriesPerPacket, int delayBetweenRetriesMillis) throws IOException {
		Map<String,Object> context = Maps.newHashMap();
		context.put("device", device);
		context.put("speed", speed);
		XBeeAddress64 xBeeAddress64 = new XBeeAddress64(xbeeAddress);
		context.put("xbeeAddress", xBeeAddress64);
		
		super.process(file, XBEE_PAGE_SIZE, ackTimeoutMillis, arduinoTimeoutSec, retriesPerPacket, delayBetweenRetriesMillis, verbose, context);
	}

	@Override
	protected void open(final Map<String, Object> context) throws Exception {
		xbee.open((String) context.get("device"), (Integer) context.get("speed"));
		
		xbee.addPacketListener(new PacketListener() {
			@Override
			public void processResponse(XBeeResponse response) {					
				if (response.getApiId() == ApiId.ZNET_RX_RESPONSE) {
					
					boolean verbose = (Boolean) context.get("verbose");
					
					if (verbose) {
						System.out.println("Received rx packet from arduino " + response);							
					}

					ZNetRxResponse zb = (ZNetRxResponse) response;
					
					if (zb.getData()[0] == MAGIC_BYTE1 && zb.getData()[1] == MAGIC_BYTE2) {
						addReply(zb.getData());
					} else {
						System.out.println("Ignoring non-programming packet " + zb);
					}
				} else if (response.getApiId() == ApiId.ZNET_TX_STATUS_RESPONSE) {
					ZNetTxStatusResponse zNetTxStatusResponse = (ZNetTxStatusResponse) response;
					
					if (zNetTxStatusResponse.isSuccess()) {
						// yay					
					} else {
						// interrupt thread in case it's waiting for ack, which will never come
						System.out.println("Failed to deliver packet. Interrupting main thread. Response: " + response);
						interrupt();
					}
				}
			}
		});
	}

	final int TX_TIMEOUT = 500;
	int counter = 1;
	
	@Override
	protected void writeData(int[] data, Map<String,Object> context) throws Exception {
		XBeeAddress64 address = (XBeeAddress64) context.get("xbeeAddress");
		//xbee.sendAsynchronous(new ZNetTxRequest(address, data));
		
		try {
			// TODO make configurable timeout
			xbee.sendSynchronous(new ZNetTxRequest(address, data), TX_TIMEOUT);			
		} catch (XBeeTimeoutException e) {
			// TODO interrupt ack thread
			// the ack will timeout so no need to do anything with this error
			System.out.println("No tx ack after " + TX_TIMEOUT + "ms");
		}
	}

	@Override
	protected void close() throws Exception {
		xbee.close();
	}

	@Override
	protected String getName() {
		return "xbee";
	}

	public final static String serialPort = "serial-port";
	public final static String baudRate = "baud-rate";
	public final String xbeeAddress = "remote-xbee-address";
	
	private void runFromCmdLine(String[] args) throws org.apache.commons.cli.ParseException, IOException {
		CliOptions cliOptions = getCliOptions();
	
		cliOptions.getOptions().addOption(
				OptionBuilder
				.withLongOpt(serialPort)
				.hasArg()
				.isRequired(true)
				.withDescription("Serial port of of local xbee (host) radio) (e.g. /dev/tty.usbserial-A6005uRz). Required")
				.create("p"));

		cliOptions.getOptions().addOption(
				OptionBuilder
				.withLongOpt(baudRate)
				.hasArg()
				.isRequired(true)
				.withType(Number.class)
				.withDescription("Baud rate of local xbee (host) radio serial port. Required")
				.create("b"));
		
		cliOptions.getOptions().addOption(
				OptionBuilder
				.withLongOpt(xbeeAddress)
				.hasArg()
				.isRequired(true)
				.withDescription("Address (64-bit) of remote XBee radio (e.g. 0013A21240AB9856)")
				.create("x"));
		
		CommandLine commandLine = cliOptions.parse(args);

		if (commandLine != null) {
			boolean verbose = false;
			
			if (commandLine.hasOption(CliOptions.verboseArg)) {
				verbose = true;
			}
			
			// cmd line
			new XBeeSketchUploader().processXBee(
					commandLine.getOptionValue(CliOptions.sketch), 
					commandLine.getOptionValue(serialPort), 
					cliOptions.getIntegerOption(baudRate), 
					commandLine.getOptionValue(xbeeAddress), 
					verbose,
					cliOptions.getIntegerOption(CliOptions.ackTimeoutMillisArg),
					cliOptions.getIntegerOption(CliOptions.arduinoTimeoutArg),
					cliOptions.getIntegerOption(CliOptions.retriesPerPacketArg),
					cliOptions.getIntegerOption(CliOptions.delayBetweenRetriesMillisArg));
		}
	}
	
	/**
	 * ex
ceylon:arduino-remote-uploader-1.0-SNAPSHOT andrew$ ./xbee-uploader.sh --sketch /Users/andrew/Documents/dev/arduino-remote-uploader/resources/BlinkSlow.cpp.hex --serial-port /dev/tty.usbserial-A6005uRz --baud-rate 9600 --remote-xbee-address "0013A200408B98FF" --arduino-timeout 0 --ack-timeout-ms 5000 --retries 50
Experimental:  JNI_OnLoad called.
Stable Library
=========================================
Native lib Version = RXTX-2.1-7
Java lib Version   = RXTX-2.1-7
Sending sketch to xbee radio, size 1102 bytes, md5 8e7a58576bdc732d3f9708dab9aea5b9, number of packets 18, and 64 bytes per packet, header ef,ac,10,a,4,4e,0,12,40,0
.......
Failed to deliver packet [page 7 of 18] on attempt 1, reason No ACK from transport device after 5000ms.. retrying
..
Failed to deliver packet [page 8 of 18] on attempt 1, reason No ACK from transport device after 5000ms.. retrying
...
Failed to deliver packet [page 10 of 18] on attempt 1, reason No ACK from transport device after 5000ms.. retrying
......
Failed to deliver packet [page 15 of 18] on attempt 1, reason No ACK from transport device after 5000ms.. retrying
....
Failed to deliver packet [page 18 of 18] on attempt 1, reason No ACK from transport device after 5000ms.. retrying
.
Successfully flashed remote Arduino in 33s, with 5 retries
ceylon:arduino-remote-uploader-1.0-SNAPSHOT andrew$ 
 
	 * @param args
	 * @throws NumberFormatException
	 * @throws IOException
	 * @throws XBeeException
	 * @throws ParseException
	 * @throws org.apache.commons.cli.ParseException
	 */
	public static void main(String[] args) throws NumberFormatException, IOException, XBeeException, ParseException, org.apache.commons.cli.ParseException {		
		initLog4j();		
		XBeeSketchUploader xBeeSketchUploader = new XBeeSketchUploader();
		xBeeSketchUploader.runFromCmdLine(args);
		//new XBeeSketchUploader().processXBee("/Users/andrew/Documents/dev/arduino-remote-uploader/resources/BlinkSlow.cpp.hex", "/dev/tty.usbserial-A6005uRz", Integer.parseInt("9600"), "0013A200408B98FF", false, 5, 0, 500, 0);
	}
}