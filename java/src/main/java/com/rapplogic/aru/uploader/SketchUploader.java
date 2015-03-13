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

package com.rapplogic.aru.uploader;

import java.io.IOException;
import java.util.Arrays;
import java.util.Map;

import org.apache.log4j.Logger;

import com.rapplogic.aru.core.Page;
import com.rapplogic.aru.core.Sketch;
import com.rapplogic.aru.core.SketchCore;

/**
 * Defines framework for uploading sketch to remote
 * 
 * @author andrew
 *
 */
public abstract class SketchUploader extends SketchCore {
	
	final Logger log = Logger.getLogger(SketchUploader.class);
	
	public final int MAGIC_BYTE1 = 0xef;
	public final int MAGIC_BYTE2 = 0xac;
	// make enum
	public final int CONTROL_PROG_REQUEST = 0x10; 	//10000
	public final int CONTROL_WRITE_EEPROM = 0x20; 	//100000
	// somewhat redundant
	public final int CONTROL_START_FLASH = 0x40; 	//1000000
	
	public final int OK = 1;
	public final int START_OVER = 2;
	public final int TIMEOUT = 3;
	
	public SketchUploader() {

	}
		
	public int[] getStartHeader(int sizeInBytes, int numPages, int bytesPerPage) {
		return new int[] { 
				MAGIC_BYTE1, 
				MAGIC_BYTE2, 
				CONTROL_PROG_REQUEST, 
				9, // xbee has length built-in but some may not (nordic w/o dynamic payload)
				(sizeInBytes >> 8) & 0xff, 
				sizeInBytes & 0xff, 
				(numPages >> 8) & 0xff, 
				numPages & 0xff,
				bytesPerPage
		};
	}
	
	// TODO consider adding retry bit to header
	
	// NOTE if header size is ever changed must also change PROG_DATA_OFFSET in library
	// xbee has error detection built-in but other protocols may need a checksum
	private int[] getHeader(int controlByte, int addressOrSize, int dataLength) {
		return new int[] {
				MAGIC_BYTE1, 
				MAGIC_BYTE2, 
				controlByte, 
				dataLength + 6, //length + 6 bytes for header
				(addressOrSize >> 8) & 0xff, 
				addressOrSize & 0xff
		};
	}

	public int[] getEEPROMWriteHeader(int address16, int dataLength) {
		return getHeader(CONTROL_WRITE_EEPROM, address16, dataLength);
	}
	
	public int[] getFlashStartHeader(int progSize) {
		return getHeader(CONTROL_START_FLASH, progSize, 0);
	}	

	protected int[] combine(int[] a, int[] b) {
		int[] result = Arrays.copyOf(a, a.length + b.length);
		System.arraycopy(b, 0, result, a.length, b.length);
		return result;
	}

	protected abstract void open(Map<String,Object> context) throws Exception;
	protected abstract void writeData(int[] data, Map<String,Object> context) throws Exception;
	protected abstract void waitForAck(int timeout) throws Exception;
	protected abstract void close() throws Exception;
	protected abstract String getName();
	
	// TODO put into superclass
	public void process(String file, int pageSize, int timeout, boolean verbose, Map<String,Object> context) throws IOException {
		// page size is max packet size for the radio
		Sketch sketch = parseSketchFromIntelHex(file, pageSize);
			
		context.put("verbose", verbose);
		
		try {
			open(context);
			
			long start = System.currentTimeMillis();
			
			System.out.println("Sending sketch to " + getName() + " radio, size " + sketch.getSize() + " bytes, md5 " + getMd5(sketch.getProgram()) + ", number of packets " + sketch.getPages().size() + ", and " + sketch.getBytesPerPage() + " bytes per packet, header " + toHex(getStartHeader(sketch.getSize(), sketch.getPages().size(), sketch.getBytesPerPage())));			

			writeData(getStartHeader(sketch.getSize(), sketch.getPages().size(), sketch.getBytesPerPage()), context);
			
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

				try {
					writeData(data, context);					
				} catch (Exception e) {
					throw new RuntimeException("Failed to deliver packet at page " + page.getOrdinal() + " of " + sketch.getPages().size(), e);
				}
				
				// don't send next page until this one is processed or we will overflow the buffer
				waitForAck(timeout);
			}

			if (!verbose) {
				System.out.println("");
			}

			System.out.println("Sending flash start packet " + toHex(getFlashStartHeader(sketch.getSize())));
			
			writeData(getFlashStartHeader(sketch.getSize()), context);
			
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
				close();
			} catch (Exception e) {}
		}
	}
}
