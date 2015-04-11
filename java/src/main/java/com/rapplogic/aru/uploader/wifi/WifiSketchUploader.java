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
import java.net.Socket;
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

public class WifiSketchUploader extends SketchUploader {
	
	public final int WIFI_PAGE_SIZE = 32;

	public WifiSketchUploader() {
		super();
	}

	public void processWifi(String file, String host, int port, final boolean verbose, int ackTimeoutMillis, int arduinoTimeoutSec, int retriesPerPacket, int delayBetweenRetriesMillis) throws IOException {
		Map<String,Object> context = Maps.newHashMap();		
		super.process(file, WIFI_PAGE_SIZE, ackTimeoutMillis, arduinoTimeoutSec, retriesPerPacket, delayBetweenRetriesMillis, verbose, context);
	}

	private Socket socket = null;
	
	@Override
	protected void open(final Map<String, Object> context) throws Exception {
		
		// open socket
		socket = new Socket("192.168.1.115", 1111);
		
		Thread t = new Thread(new Runnable() {
			
			@Override
			public void run() {
				int ch = 0;
				
				// reply always terminated with 13,10
				try {
					while ((ch = socket.getInputStream().read()) > -1) {
						if (ch > 32 && ch < 127) {

						} else {
						
						}
						
						if (ch == 10) {

						}
					}
					
					System.out.println("Input stream closing");
				} catch (IOException e) {
					e.printStackTrace();
				}
			}
		});
		
		t.setDaemon(true);
		t.start();
		
		// esp8266 needs short delay after connecting
		Thread.sleep(50);
		
		
//		addReply(reply);
		// add listener
	}

	final int TX_TIMEOUT = 500;
	int counter = 1;
	
	@Override
	protected void writeData(int[] data, Map<String,Object> context) throws Exception {
		// ugh, convert to byte[] required or esp will receive multiple IPD commands
		byte[] b = new byte[data.length];
		
		for (int i = 0; i < data.length; i++) {
			b[i] = (byte) (data[i] & 0xff);
		}
		
		socket.getOutputStream().write(b);
	}

	@Override
	protected void close() throws Exception {
		// close socket
		socket.close();
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