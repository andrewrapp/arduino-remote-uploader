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

import java.util.Arrays;

import org.apache.log4j.ConsoleAppender;
import org.apache.log4j.Level;
import org.apache.log4j.Logger;
import org.apache.log4j.PatternLayout;

import com.rapplogic.aru.core.SketchCore;

/**
 * Parses a intel hex AVR/Arduino Program into an object representation with a user defined page-size
 * 
 * @author andrew
 *
 */
public class SketchUploader extends SketchCore {
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
	
	protected static void initLog4j() {
		  ConsoleAppender console = new ConsoleAppender();
		  String PATTERN = "%d [%p|%c|%C{1}] %m%n";
		  console.setLayout(new PatternLayout(PATTERN)); 
		  console.activateOptions();
		  // only log this package
		  Logger.getLogger(SketchUploader.class.getPackage().getName()).addAppender(console);
		  Logger.getLogger(SketchUploader.class.getPackage().getName()).setLevel(Level.ERROR);
		  Logger.getRootLogger().addAppender(console);
		  // quiet logger
		  Logger.getRootLogger().setLevel(Level.ERROR);
	}
}
