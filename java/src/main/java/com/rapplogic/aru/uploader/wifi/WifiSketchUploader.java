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

package com.rapplogic.aru.uploader.wifi;

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
public class WifiSketchUploader extends SketchUploader {
	
	final Queue<int[]> replies = Lists.newLinkedList();

	public final int WIFI_PAGE_SIZE = 32;

	private final XBee xbee = new XBee();
	
	public WifiSketchUploader() {
		super();
	}

	public void processWifi(String file, String host, int port, final boolean verbose, int ackTimeoutMillis, int arduinoTimeoutSec, int retriesPerPacket, int delayBetweenRetriesMillis) throws IOException {
		Map<String,Object> context = Maps.newHashMap();		
		super.process(file, WIFI_PAGE_SIZE, ackTimeoutMillis, arduinoTimeoutSec, retriesPerPacket, delayBetweenRetriesMillis, verbose, context);
	}

	@Override
	protected void open(final Map<String, Object> context) throws Exception {
		
		// open socket
		
		// add listener
		
//		xbee.addPacketListener(new PacketListener() {
//			@Override
//			public void processResponse(XBeeResponse response) {					
//				if (response.getApiId() == ApiId.ZNET_RX_RESPONSE) {
//					
//					boolean verbose = (Boolean) context.get("verbose");
//					
//					if (verbose) {
//						System.out.println("Received rx packet from arduino " + response);							
//					}
//
//					ZNetRxResponse zb = (ZNetRxResponse) response;
//					
//					if (zb.getData()[0] == MAGIC_BYTE1 && zb.getData()[1] == MAGIC_BYTE2) {
//						addReply(zb.getData());
//					} else {
//						System.out.println("Ignoring non-programming packet " + zb);
//					}
//				} else if (response.getApiId() == ApiId.ZNET_TX_STATUS_RESPONSE) {
//					ZNetTxStatusResponse zNetTxStatusResponse = (ZNetTxStatusResponse) response;
//					
//					if (zNetTxStatusResponse.isSuccess()) {
//						// yay					
//					} else {
//						// interrupt thread in case it's waiting for ack, which will never come
//						System.out.println("Failed to deliver packet. Interrupting main thread. Response: " + response);
//						interrupt();
//					}
//				}
//			}
//		});
	}

	final int TX_TIMEOUT = 500;
	int counter = 1;
	
	@Override
	protected void writeData(int[] data, Map<String,Object> context) throws Exception {
		// write to socket
	}

	@Override
	protected void close() throws Exception {
		// close socket
	}

	@Override
	protected String getName() {
		return "wifi";
	}

	public final static String host = "host";
	public final static String port = "port";
	
	private void runFromCmdLine(String[] args) throws org.apache.commons.cli.ParseException, IOException {
		CliOptions cliOptions = getCliOptions();
	
		cliOptions.getOptions().addOption(
				OptionBuilder
				.withLongOpt(host)
				.hasArg()
				.isRequired(true)
				.withDescription("Host ip address of wifi device. Required")
				.create("p"));

		cliOptions.getOptions().addOption(
				OptionBuilder
				.withLongOpt(port)
				.hasArg()
				.isRequired(true)
				.withType(Number.class)
				.withDescription("Port of wifi server. Required")
				.create("b"));
		
		CommandLine commandLine = cliOptions.parse(args);

		if (commandLine != null) {
			boolean verbose = false;
			
			if (commandLine.hasOption(CliOptions.verboseArg)) {
				verbose = true;
			}
			
			// cmd line
			new WifiSketchUploader().processWifi(
					commandLine.getOptionValue(CliOptions.sketch), 
					commandLine.getOptionValue(host), 
					cliOptions.getIntegerOption(port), 
					verbose,
					cliOptions.getIntegerOption(CliOptions.ackTimeoutMillisArg),
					cliOptions.getIntegerOption(CliOptions.arduinoTimeoutArg),
					cliOptions.getIntegerOption(CliOptions.retriesPerPacketArg),
					cliOptions.getIntegerOption(CliOptions.delayBetweenRetriesMillisArg));
		}
	}
	
	public static void main(String[] args) throws NumberFormatException, IOException, XBeeException, ParseException, org.apache.commons.cli.ParseException {		
		initLog4j();		
		WifiSketchUploader xBeeSketchUploader = new WifiSketchUploader();
		xBeeSketchUploader.runFromCmdLine(args);
	}
	
}