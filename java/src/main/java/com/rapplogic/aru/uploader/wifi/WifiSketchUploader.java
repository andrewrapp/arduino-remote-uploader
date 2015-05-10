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
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketException;
import java.text.ParseException;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;

import org.apache.commons.cli.CommandLine;
import org.apache.commons.cli.OptionBuilder;

import com.google.common.collect.Maps;
import com.rapplogic.aru.uploader.CliOptions;
import com.rapplogic.aru.uploader.SketchUploader;
import com.rapplogic.xbee.api.XBeeException;

public class WifiSketchUploader extends SketchUploader {
	
	// this is up for debate. esp seems to drop more packets around low forties. and fails outright around 46
	public final int WIFI_PAGE_SIZE = 32;
	private Socket socket = null;
	private volatile boolean connected;
	
	public final static String host = "host";
	public final static String port = "port";
	public final static String connectionTimeoutSecs = "socket-connection-timeout-s";
	public final static String readTimeoutSecs = "socket-read-timeout-s";	
	
	public WifiSketchUploader() {
		super();
	}

	public void flash(String file, String host, int port, int connectionTimeoutSecs, int readTimeoutSecs, final boolean verbose, int ackTimeoutMillis, int arduinoTimeoutSecs, int retriesPerPacket, int delayBetweenRetriesMillis) throws IOException {
		Map<String,Object> context = Maps.newHashMap();
		
		context.put("host", host);
		context.put("port", port);
		context.put("connectionTimeoutSecs", connectionTimeoutSecs);
		context.put("readTimeoutSecs", readTimeoutSecs);
		
		// determine max data we can send with each programming packet
		// NOTE remember to size the array on the Arduino accordingly to accomadate this size
		int pageSize = WIFI_PAGE_SIZE - getProgramPageHeader(0, 0).length;
		super.process(file, pageSize, ackTimeoutMillis, arduinoTimeoutSecs, retriesPerPacket, delayBetweenRetriesMillis, verbose, context);
	}

//	private int[] toIntArray(List<Integer> list) {
//		int[] reply = new int[list.size()];
//		for (int i = 0; i < list.size(); i++) {
//			reply[i] = list.get(i);
//		}
//		
//		return reply;
//	}	

	private String intArrayToString(int[] arr) {
		StringBuilder stringBuilder = new StringBuilder();
		
		for (int i = 0; i < arr.length; i++) {
			stringBuilder.append(arr[i] + ",");
		}
		
		return stringBuilder.toString();
	}
	
	private Thread t;
	
	@Override
	protected void open(final Map<String, Object> context) throws Exception {
		
		String host = (String) context.get("host");
		Integer port = (Integer) context.get("port");
		Integer connectionTimeoutSecs = (Integer) context.get("connectionTimeoutSecs");
		Integer readTimeoutSecs = (Integer) context.get("readTimeoutSecs");		
		
		// open socket
		socket = new Socket();
		socket.connect(new InetSocketAddress(host, port), connectionTimeoutSecs*1000);
		socket.setSoTimeout(readTimeoutSecs*1000);
		
		connected = true;
		
		final int[] reply = new int[5];
		final AtomicInteger replyIndex = new AtomicInteger(0);

		// TODO unlike other wireless protocols, wifi is stateful so we need to handle situations where we lose the socket and reconnect
		
		t = new Thread(new Runnable() {			
			@Override
			public void run() {
				int ch = 0;
				
				// reply always terminated with 13,10
				try {
					while ((ch = socket.getInputStream().read()) > -1) {
						if (replyIndex.get() < reply.length) {
							reply[replyIndex.getAndIncrement()] = ch;							
						} else if (replyIndex.get() == 5 && ch == 13) {
							replyIndex.getAndIncrement();
							// discard
						} else if (replyIndex.get() == 6 && ch == 10) {
							//System.out.println("reply is " + stringBuilder.toString());
	//						stringBuilder = new StringBuilder();
							handleReply(reply);
							replyIndex.set(0);
						} else {
							//error
							throw new RuntimeException("Expected CR/LF -- invalid reply at position " + replyIndex.get() + ", array: " + intArrayToString(reply));
						}
					}
				} catch (IOException e) {
					if (!connected && e instanceof SocketException) {
						// expected.. ignore
					} else {
						System.out.println("IO error in socket reader");
						e.printStackTrace();		
					}
				} catch (Exception e) {
					System.out.println("Unexpected error in socket reader");
					e.printStackTrace();
				}
				
				connected = false;
			}
		});
		
		t.setDaemon(true);
		t.start();
	}
	
	protected boolean isConnected() {
		return connected;
	}
	
	private void handleReply(int[] reply) {
		//System.out.println("Received reply " + intArrayToString(reply));
		// { MAGIC_BYTE1, MAGIC_BYTE2, 0, 0, 0};
		addReply(reply);
	}
	
	@Override
	protected void writeData(int[] data, Map<String,Object> context) throws Exception {
		// ugh, convert to byte[] required or esp will receive multiple IPD commands
		byte[] b = new byte[data.length + 2];
		
		for (int i = 0; i < data.length; i++) {
			b[i] = (byte) (data[i] & 0xff);
		}
		
		// every transmission must end with CR/LF
		b[data.length] = (byte)13;
		b[data.length + 1] = (byte)10;
		
		socket.getOutputStream().write(b);
	}

	@Override
	protected void close() throws Exception {
		connected = false;
		// close socket
		socket.close();
	}

	@Override
	protected String getName() {
		return "wifi";
	}
	
	private void runFromCmdLine(String[] args) throws org.apache.commons.cli.ParseException, IOException {
		CliOptions cliOptions = getCliOptions();
	
		cliOptions.addOption(
				OptionBuilder
				.withLongOpt(host)
				.hasArg()
				.isRequired(true)
				.withDescription("Host ip address of wifi device. Required")
				.create("x")); // single arg is optional

		cliOptions.addOption(
				OptionBuilder
				.withLongOpt(port)
				.hasArg()
				.isRequired(true)
				.withType(Number.class)
				.withDescription("Port of wifi server. Required")
				.create("p"));

		cliOptions.getDefaults().put(connectionTimeoutSecs, "10");
		
		cliOptions.addOption(
				OptionBuilder
				.withLongOpt(connectionTimeoutSecs)
				.hasArg()
				.isRequired(false)
				.withType(Number.class)
				.withDescription("Connection timeout seconds")
				.create("q")); // arbitrary -- running out of letters
		
		cliOptions.getDefaults().put(readTimeoutSecs, "10");
		
		cliOptions.addOption(
				OptionBuilder
				.withLongOpt(readTimeoutSecs)
				.hasArg()
				.isRequired(false)
				.withType(Number.class)
				.withDescription("Connection read seconds")
				.create("v"));
		
		cliOptions.build();
		
		CommandLine commandLine = cliOptions.parse(args);

		if (commandLine != null) {						
			new WifiSketchUploader().flash(
					commandLine.getOptionValue(CliOptions.sketch), 
					commandLine.getOptionValue(host), 
					cliOptions.getIntegerOption(port), 
					cliOptions.getIntegerOption(connectionTimeoutSecs),
					cliOptions.getIntegerOption(readTimeoutSecs),
					commandLine.hasOption(CliOptions.verboseArg),
					cliOptions.getIntegerOption(CliOptions.ackTimeoutMillisArg),
					cliOptions.getIntegerOption(CliOptions.arduinoTimeoutArg),
					cliOptions.getIntegerOption(CliOptions.retriesPerPacketArg),
					cliOptions.getIntegerOption(CliOptions.delayBetweenRetriesMillisArg));
		}
	}
	
	/**
	 * ex
ceylon:arduino-remote-uploader-1.0-SNAPSHOT andrew$ ./wifi-uploader.sh  -d --host 192.168.1.115  --port 1111 -s ../../../resources/BlinkSlow.cpp.hex 
Sending sketch to wifi radio, size 1102 bytes, md5 8e7a58576bdc732d3f9708dab9aea5b9, number of packets 43, and 26 bytes per packet, header ef,ac,10,a,4,4e,0,2b,1a,3c
Sending page 1 of 43, with address 0, length 32, packet ef,ac,20,20,0,0,c,94,61,0,c,94,7e,0,c,94,7e,0,c,94,7e,0,c,94,7e,0,c,94,7e,0,c,94
Sending page 2 of 43, with address 26, length 32, packet ef,ac,20,20,0,1a,7e,0,c,94,7e,0,c,94,7e,0,c,94,7e,0,c,94,7e,0,c,94,7e,0,c,94,7e,0
..
Sending flash start packet ef,ac,40,6,4,4e
Sending flash packet to radio ef,ac,40,6,4,4e
Successfully flashed remote Arduino in 7s, with 0 retries

	 * @param args
	 * @throws NumberFormatException
	 * @throws IOException
	 * @throws XBeeException
	 * @throws ParseException
	 * @throws org.apache.commons.cli.ParseException
	 */
	public static void main(String[] args) throws NumberFormatException, IOException, ParseException, org.apache.commons.cli.ParseException {		
		initLog4j();		
		WifiSketchUploader wifiSketchUploader = new WifiSketchUploader();
		wifiSketchUploader.runFromCmdLine(args);
//		new WifiSketchUploader().flash(
//				"/Users/andrew/Documents/dev/arduino-remote-uploader/resources/BlinkFast.cpp.hex", 
//				"192.168.1.115", 
//				1111,
//				10,
//				10,
//				true,
//				3000,
//				5,
//				3,
//				0);
	}
}