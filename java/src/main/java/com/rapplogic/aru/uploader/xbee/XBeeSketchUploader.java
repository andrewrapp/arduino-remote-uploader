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

import com.rapplogic.xbee.XBeeConnection;
import com.rapplogic.xbee.socket.SocketXBeeConnection;
import org.apache.commons.cli.CommandLine;
import org.apache.commons.cli.OptionBuilder;

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
import com.rapplogic.xbee.api.wpan.RxResponse;
import com.rapplogic.xbee.api.wpan.RxResponse64;
import com.rapplogic.xbee.api.wpan.TxRequest64;
import com.rapplogic.xbee.api.wpan.TxStatusResponse;
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

	// TODO make this run as a server where it's connected to the radios. control via curl api
	// TODO xbee just woke, send programming!
	//final int WAKE = 4;
	// block size for eeprom writes
	// TODO make this configurable. series 1 radios can support a larger payload
	public final int XBEE_PAGE_SIZE = 64;
	private int xbeeTxAckTimeoutMs = 500;

	private final XBee xbee = new XBee();
	
	public enum Series { SERIES1, SERIES2 };
	
	public XBeeSketchUploader() {
		super();
	}

	public void flash(String file, String radioType, XBeeConnectionType xBeeConnectionType, String xbeeAddress, final boolean verbose, int ackTimeoutMillis, int arduinoTimeoutSec, int retriesPerPacket, int delayBetweenRetriesMillis) throws IOException {
		Map<String,Object> context = Maps.newHashMap();
		context.put("connectionType", xBeeConnectionType);

		XBeeAddress64 xBeeAddress64 = null;
		
		try {
			xBeeAddress64 = new XBeeAddress64(xbeeAddress);
		} catch (Exception e) {
			throw new RuntimeException("Invalid xbee 64-bit address " + xbeeAddress);
		}
		
		context.put("xbeeAddress", xBeeAddress64);
		
		super.process(file, XBEE_PAGE_SIZE, ackTimeoutMillis, arduinoTimeoutSec, retriesPerPacket, delayBetweenRetriesMillis, verbose, context);
	}

	@Override
	protected void open(final Map<String, Object> context) throws Exception {

		if (context.get("connectionType") instanceof LocalConnection) {
			LocalConnection localConnection = (LocalConnection) context.get("connectionType");
			xbee.open((String) localConnection.getSerialPort(), localConnection.getBaudRate());
		} else {
			SerialServerConnection ser = (SerialServerConnection) context.get("connectionType");
			xbee.initProviderConnection((XBeeConnection)new SocketXBeeConnection(ser.getHost(), ser.getPort()));
		}

		final boolean verbose = (Boolean) context.get("verbose");
		
		xbee.addPacketListener(new PacketListener() {
			@Override
			public void processResponse(XBeeResponse response) {
				if (response.getApiId() == ApiId.ZNET_RX_RESPONSE || response.getApiId() == ApiId.RX_64_RESPONSE) {	
					if (verbose) {
						System.out.println("Received rx packet from arduino " + response);							
					}

					if (response.getApiId() == ApiId.ZNET_RX_RESPONSE) {
						ZNetRxResponse zb = (ZNetRxResponse) response;
						
						if (zb.getData()[0] == MAGIC_BYTE1 && zb.getData()[1] == MAGIC_BYTE2) {
							addReply(zb.getData());
						} else {
							System.out.println("Ignoring non-programming packet " + zb);
						}						
					} else {
						RxResponse rx64 = (RxResponse64) response;
						
						if (rx64.getData()[0] == MAGIC_BYTE1 && rx64.getData()[1] == MAGIC_BYTE2) {
							addReply(rx64.getData());
						} else {
							System.out.println("Ignoring non-programming packet " + rx64);
						}							
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
				} else if (response.getApiId() == ApiId.TX_STATUS_RESPONSE) {
					TxStatusResponse txStatusResponse = (TxStatusResponse) response;
					
					if (txStatusResponse.isSuccess()) {
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

	int counter = 1;
	
	@Override
	protected void writeData(int[] data, Map<String,Object> context) throws Exception {
		Series series = (Series) context.get("series");
		XBeeAddress64 address = (XBeeAddress64) context.get("xbeeAddress");

		try {
			// TODO make configurable timeout
			if (series == Series.SERIES1) {
				xbee.sendSynchronous(new TxRequest64(address, data), xbeeTxAckTimeoutMs);
			} else {
				//xbee.sendAsynchronous(new ZNetTxRequest(address, data));
				xbee.sendSynchronous(new ZNetTxRequest(address, data), xbeeTxAckTimeoutMs);
			}
		} catch (XBeeTimeoutException e) {
			// TODO interrupt ack thread
			// the ack will timeout so no need to do anything with this error
			System.out.println("No tx ack from receiving XBee after " + xbeeTxAckTimeoutMs + "ms");
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

	public final static String radioType = "radio-type";
	public final static String serialPort = "serial-port";
	public final static String baudRate = "baud-rate";
	public final static String serialServerHost = "serial-server-host";
	public final static String serialServerPort = "serial-server-port";
	public final String xbeeAddress = "remote-xbee-address";
	public final String xbeeTxAckTimeout = "xbee-tx-ack-timeout";
	
	private void runFromCmdLine(String[] args) throws org.apache.commons.cli.ParseException, IOException {
		CliOptions cliOptions = getCliOptions();

		cliOptions.addOption(
				OptionBuilder
				.withLongOpt(radioType)
				.hasArg()
				.isRequired(true)
				.withDescription("XBee radio series: must be either series1 or series2. Required")
				.create("i"));
		
		cliOptions.addOption(
				OptionBuilder
				.withLongOpt(xbeeAddress)
				.hasArg()
				.isRequired(true)
				.withDescription("Address (64-bit) of remote XBee radio (e.g. 0013A21240AB9856)")
				.create("x"));

		cliOptions.addOption(
				OptionBuilder
						.withLongOpt(serialPort)
						.hasArg()
						.isRequired(false)
						.withDescription("Serial port of of local xbee (host) radio) (e.g. /dev/tty.usbserial-A6005uRz)")
						.create("p"));

		cliOptions.addOption(
				OptionBuilder
						.withLongOpt(baudRate)
						.hasArg()
						.isRequired(false)
						.withType(Number.class)
						.withDescription("Baud rate of local xbee (host) radio serial port")
						.create("b"));

		cliOptions.addOption(
				OptionBuilder
						.withLongOpt(serialServerHost)
						.hasArg()
						.isRequired(false)
						.withDescription("Host or ip address of xbee serial server")
						.create("j"));

		cliOptions.addOption(
				OptionBuilder
						.withLongOpt(serialServerPort)
						.hasArg()
						.isRequired(false)
						.withType(Number.class)
						.withDescription("Listen port of xbee serial server")
						.create("k"));

		cliOptions.addOption(
				OptionBuilder
						.withLongOpt(xbeeTxAckTimeout)
						.hasArg()
						.isRequired(false)
						.withType(Number.class)
						.withDescription("How long to wait to receive and an ACK from the receiving XBee in ms. Although flashing can succeed if these timeous occur, it's best to wait for an ACK before sending the next comand. Bump this up when using a serial server. Default is " + xbeeTxAckTimeoutMs + " ms")
						.create("l"));

		cliOptions.build();
		
		CommandLine commandLine = cliOptions.parse(args);

		if (commandLine != null) {
			
			// validate
			try {
				Series series = Series.valueOf(commandLine.getOptionValue(radioType).toUpperCase());				
			} catch (Exception e) {
				System.err.println(radioType + " must be series1 or series2");
				return;
			}

			if (commandLine.getOptionValue(xbeeTxAckTimeout) != null) {
				xbeeTxAckTimeoutMs = cliOptions.getIntegerOption(xbeeTxAckTimeout);
			}

			XBeeConnectionType xBeeConnectionType = null;

			if ((commandLine.getOptionValue(serialPort) == null || commandLine.getOptionValue(baudRate) == null) &&
					(commandLine.getOptionValue(serialServerHost) == null || commandLine.getOptionValue(serialServerPort) == null)) {
				System.err.println("Either a serial port and baudrate must be specified or a serial server host/port");
				return;
			} else {
				if (commandLine.getOptionValue(serialPort) != null) {
					xBeeConnectionType = new LocalConnection(commandLine.getOptionValue(serialPort), cliOptions.getIntegerOption(baudRate));
				} else {
					xBeeConnectionType = new SerialServerConnection(commandLine.getOptionValue(serialServerHost), cliOptions.getIntegerOption(serialServerPort));
				}
			}

			// cmd line
			flash(
					commandLine.getOptionValue(CliOptions.sketch),
					commandLine.getOptionValue(radioType),
					xBeeConnectionType,
					commandLine.getOptionValue(xbeeAddress),
					commandLine.hasOption(CliOptions.verboseArg),
					cliOptions.getIntegerOption(CliOptions.ackTimeoutMillisArg),
					cliOptions.getIntegerOption(CliOptions.arduinoTimeoutArg),
					cliOptions.getIntegerOption(CliOptions.retriesPerPacketArg),
					cliOptions.getIntegerOption(CliOptions.delayBetweenRetriesMillisArg));
		}
	}
	
	/**
	 * ex
./xbee-uploader.sh --sketch /Users/andrew/Documents/dev/arduino-remote-uploader/resources/BlinkFast-atmega-328-16Mhz.cpp.hex --serial-port /dev/tty.usbserial-A6005uRz --baud-rate 9600 --radio-type SERIES2 --remote-xbee-address "0013A200408B98FF" --arduino-timeout-s 0 --ack-timeout-ms 5000 --retries 50

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

	static class XBeeConnectionType {

	}

	static class SerialServerConnection extends XBeeConnectionType {
		private String host;
		private int port;

		public SerialServerConnection(String host, int port) {
			this.host = host;
			this.port = port;
		}

		public String getHost() {
			return host;
		}

		public int getPort() {
			return port;
		}
	}

	static class LocalConnection extends XBeeConnectionType {
		private String serialPort;
		private int baudRate;

		public String getSerialPort() {
			return serialPort;
		}

		public int getBaudRate() {
			return baudRate;
		}

		public LocalConnection(String serialPort, int baudRate) {
			this.serialPort = serialPort;
			this.baudRate = baudRate;
		}
	}
}