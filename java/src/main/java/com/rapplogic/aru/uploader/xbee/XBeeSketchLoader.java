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
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.ReentrantLock;

import org.apache.commons.cli.CommandLine;
import org.apache.commons.cli.CommandLineParser;
import org.apache.commons.cli.Options;
import org.apache.commons.cli.PosixParser;

import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.rapplogic.aru.uploader.SketchUploader;
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
public class XBeeSketchLoader extends SketchUploader {
	
	final List<Integer> messages = Lists.newArrayList();
		
	// xbee just woke, send programming now!
	//final int WAKE = 4;
	
	// block size for eeprom writes
	public final int XBEE_PAGE_SIZE = 64;
	
	final ReentrantLock lock = new ReentrantLock();
	final Condition rxPacketCondition = lock.newCondition();
	
	public XBeeSketchLoader() {
		super();
	}
	
	public void waitForAck(final int timeout) throws InterruptedException {
		long now = System.currentTimeMillis();
		// TODO send this timeout to arduino in prog start header
		
		lock.lockInterruptibly();
		
		try {
			if (rxPacketCondition.await(timeout, TimeUnit.SECONDS)) {
				int reply = messages.get(0).intValue();
				
				switch (reply) {
				case OK:
					break;
				case START_OVER:
					throw new RuntimeException("Upload failed: arduino received a program packet when it was not in prog mode.. start over");
				case TIMEOUT:
					throw new RuntimeException("Upload failed:: arduino had not received a packet for " + timeout + " secs and timed-out.. start over");
				default:
					throw new RuntimeException("Unexpected response code from arduino: " + toHex(new int[] {reply}));						
				}				
			} else {
				throw new RuntimeException("No reply from Arduino after " + timeout + " seconds");				
			}				
		} finally {
			lock.unlock();
		}
	}
	
	private XBee xbee = new XBee();
	
	public void process(String file, String device, int speed, String xbeeAddress, final boolean verbose, int timeout) throws IOException {
		Map<String,Object> context = Maps.newHashMap();
		context.put("device", device);
		context.put("speed", speed);
		XBeeAddress64 xBeeAddress64 = new XBeeAddress64(xbeeAddress);
		context.put("xbeeAddress", xBeeAddress64);
		
		super.process(file, XBEE_PAGE_SIZE, timeout, verbose, context);
	}

	private static void runFromCmdLine(String[] args) throws org.apache.commons.cli.ParseException, IOException {
		Options options = new Options();
		
		// cli doesn't allow single dashes in args which is insane!
		final String sketch = "sketch_hex";
		final String serialPort = "serial_port";
		final String baudRate = "baud_rate";
		final String xbeeAddress = "remote_xbee_address";
		final String verboseArg = "verbose";
		final String timeoutArg = "timeout";
		
		// add t option
		options.addOption(sketch, true, "Path to compiled sketch (compiled by Arduino IDE)");
		options.addOption(serialPort, true, "Serial port of XBee radio (e.g. /dev/tty.usbserial-A6005uRz)");
		options.addOption(baudRate, true, "Baud rate of host XBee baud rate configuration");
		options.addOption(xbeeAddress, true, "Address (64-bit) of remote XBee radio (e.g. 0013A21240AB9856)");
		options.addOption(verboseArg, false, "Make chatty");
		options.addOption(timeoutArg, false, "No reply timeout");
		
		CommandLineParser parser = new PosixParser();
		CommandLine cmd = parser.parse(options, args);

		boolean verbose = false;
		
		if (cmd.hasOption(verboseArg)) {
			verbose = true;
		}
		
		int baud = 0;
		
		try {
			baud = Integer.parseInt(cmd.getOptionValue(baudRate));
		} catch (Exception e) {
			System.err.println("Baud rate is required and must be a positive integer");
			return;
		}
		
		int timeout = 10;

		if (cmd.hasOption(timeoutArg)) {
			try {
				timeout = Integer.parseInt(cmd.getOptionValue(timeoutArg));
			} catch (NumberFormatException e) {
				System.err.println("Timeout must be a positive integer");
				return;
			}
			// TODO validate size is within 1-255
		}
		
		// cmd line
		new XBeeSketchLoader().process(cmd.getOptionValue(sketch), cmd.getOptionValue(serialPort), baud, cmd.getOptionValue(xbeeAddress), verbose, timeout);
	}
	
	public static void main(String[] args) throws NumberFormatException, IOException, XBeeException, ParseException, org.apache.commons.cli.ParseException {		
		initLog4j();
		
		if (false) {
			runFromCmdLine(args);
		} else {
			// run from eclipse for dev
			new XBeeSketchLoader().process("/Users/andrew/Documents/dev/arduino-remote-uploader/resources/BlinkFast.cpp.hex", "/dev/tty.usbserial-A6005uRz", Integer.parseInt("9600"), "0013A200408B98FF", false, 5);
//			new XBeeSketchLoader().process("/Users/andrew/Documents/dev/arduino-remote-uploader/resources/BlinkSlow.cpp.hex", "/dev/tty.usbserial-A6005uRz", Integer.parseInt("9600"), "0013A200408B98FF", false, 5);
		}
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
						System.out.println("Received message from arduino " + response);							
					}

					ZNetRxResponse zb = (ZNetRxResponse) response;
					messages.clear();
					
					if (zb.getData()[0] == MAGIC_BYTE1 && zb.getData()[1] == MAGIC_BYTE2) {
						try {
							lock.lockInterruptibly();
						} catch (InterruptedException e) {
							// interrupted (kill signal)
							return;
						}
						
						try {
							messages.add(zb.getData()[2]);
							rxPacketCondition.signal();								
						} finally {
							lock.unlock();
						}
					
					} else {
						System.out.println("Ignoring non-programming packet " + zb);
					}
				}
			}
		});
	}

	@Override
	protected void writeData(int[] data, Map<String,Object> context) throws Exception {
		XBeeAddress64 address = (XBeeAddress64) context.get("xbeeAddress");
		
		ZNetTxStatusResponse response = (ZNetTxStatusResponse) xbee.sendSynchronous(new ZNetTxRequest(address, data));
		
		if (response.isSuccess() || response.getDeliveryStatus() != DeliveryStatus.SUCCESS) {
//			System.out.print("#");					
		} else {
			// TODO handle retries
			// that radio is powered on
			throw new RuntimeException("Failed to deliver packet. response " + response);
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
}