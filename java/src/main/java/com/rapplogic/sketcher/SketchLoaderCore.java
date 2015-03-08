/**
 * Copyright (c) 2015 Andrew Rapp. All rights reserved.
 *
 * This file is part of arduino-sketcher
 *
 * arduino-sketcher is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * arduino-sketcher is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with arduino-sketcher.  If not, see <http://www.gnu.org/licenses/>.
 */

package com.rapplogic.sketcher;

import java.io.File;
import java.io.IOException;
import java.nio.charset.Charset;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;
import java.util.List;

import org.apache.commons.codec.binary.Hex;
import org.apache.log4j.ConsoleAppender;
import org.apache.log4j.Level;
import org.apache.log4j.Logger;
import org.apache.log4j.PatternLayout;

import com.google.common.collect.Lists;
import com.google.common.io.Files;

/**
 * Parses a intel hex AVR/Arduino Program into an object representation with a user defined page-size
 * 
 * @author andrew
 *
 */
public class SketchLoaderCore {

	public final int ARDUINO_PAGE_SIZE = 128;
	public final int MAX_PROGRAM_SIZE = 0x20000;

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
	
	public SketchLoaderCore() {

	}
	
	public String getMd5(int[] program) {
		MessageDigest messageDigest;
		
		try {
			messageDigest = MessageDigest.getInstance("MD5");
		} catch (NoSuchAlgorithmException e) {
			//srlsy??
			throw new RuntimeException("", e);
		}
		
		for (int i = 0; i < program.length; i++) {
			messageDigest.update((byte)i);	
		}
		
		return new String(Hex.encodeHex(messageDigest.digest()));
	}
	
	
	/**
	 * Parse an intel hex file into an array of bytes
	 * 
	 * @param file
	 * @return
	 * @throws IOException
	 */
	public int[] parseIntelHex(String file) throws IOException {
		File hexFile = new File(file);
				
		int[] program = new int[MAX_PROGRAM_SIZE];
				
    	// example:
		// length = 0x10
		// addr = 0000
		// type = 00
		//:100000000C94C7010C94EF010C94EF010C94EF01D8
		//:061860000994F894FFCF8B
		//:00000001FF
		
    	List<String> hex = Files.readLines(hexFile, Charset.forName("UTF-8"));
    	
		int lineCount = 0;
		int position = 0;

    	for (String line : hex) {

    		String dataLine = line.split(":")[1];
    		
    		//System.out.println("line: " + ++lineCount + ": " + line);

    		int checksum = 0;
    		
    		if (line.length() > 10) {
	    		int length = Integer.decode("0x" + dataLine.substring(0, 2));
	    		int address = Integer.decode("0x" + dataLine.substring(2, 6));
	    		int type = Integer.decode("0x" + dataLine.substring(6, 8));	    			
    		
	    		checksum += length + address + type;

	    		//System.out.println("Length is " + length + ", address is " + Integer.toHexString(address) + ", type is " + Integer.toHexString(type));
	    		
                // Ignore all record types except 00, which is a data record. 
                // Look out for 02 records which set the high order byte of the address space
                if (type == 0) {
                    // Normal data record
                } else if (type == 4 && length == 2 && address == 0 && line.length() > 12) {
                	// Address > 16bit not supported by Arduino so not important
                	throw new RuntimeException("Record type 4 is not implemented");
                } else {
                	//System.out.println("Skipped: " + line);
                    continue;
                }
                
                // verify the addr matches our current array position
                if (position > 0 && position != address) {
                	throw new RuntimeException("Expected address of " + position + " but was " + address);
                }
                
                // address will always be last position or we'd have gaps in the array
                position = address;
                
                {
                	int i = 8;
	                // data starts at 8 (4th byte) to end minus checksum
	                for (;i < 8 + (length*2); i+=2) {
	                    int b = Integer.decode("0x" + dataLine.substring(i, i+2));	
	                    checksum+= b;
	                    // what we're doing here is simply parsing the data portion of each line and adding to the array
	                    
	                    if (position >= MAX_PROGRAM_SIZE) {
	                    	throw new RuntimeException("Program is too large for Arduino. Max size is " + MAX_PROGRAM_SIZE);
	                    }
	                    
	                    program[position] = b;
	                    position++;
	                }	     
	                
	                //System.out.println("Program data: " + toHex(program, position - length, length));
	                
	                // we should be at the checksum position
	                if (i != dataLine.length() - 2) {
	                	throw new RuntimeException("Line length does not match expected length " + length);
	                }
	                
	                // checksum
	                int expectedChecksum = Integer.decode("0x" + line.substring(line.length() - 2, line.length()));
	                //System.out.println("Expected checksum is " + line.substring(line.length() - 2, line.length()));
	                
	                //checksum+= expectedChecksum;
	                // TODO somethings not right
	                checksum = 0xff - checksum & 0xff;
	                
	                //System.out.println("Checksum is " + Integer.toHexString(checksum & 0xff));
                }  
    		}
    	}
    	
    	int[] resize = new int[position];
    	System.arraycopy(program, 0, resize, 0, position);
    	
    	return resize;
	}
    
    public String toHex(int[] data, int offset, int length) {
    	StringBuilder hex = new StringBuilder();
    	
    	for (int i = offset; i < offset + length; i++) {
    		hex.append(Integer.toHexString(data[i]));
    		if (i != data.length - 1) {
    			hex.append(",");
    		}
    	}
    	
    	return hex.toString();    	
    }
    
    public String toHex(int[] data) {
    	return toHex(data, 0, data.length);
    }

	public Sketch parseSketchFromIntelHex(String fileName, int pageSize) throws IOException {	
		
		int[] program = parseIntelHex(fileName);
		
		List<Page> pages = Lists.newArrayList();		
		System.out.println("Program length is " + program.length + ", page size is " + pageSize);
		
		int position = 0;
		int count = 0;
		// write the program to the arduino in chunks of ARDUINO_BLOB_SIZE
		while (position < program.length) {
			
			int length = 0;
			
			if (position + pageSize < program.length) {
				length = pageSize;
			} else {
				length = program.length - position;
			}

//			System.out.println("Creating page for " + toHex(program, position, length));
			pages.add(new Page(program, position, length, count));
			
			// index to next position
			position+=length;
			count++;
		}
		
		return new Sketch(program.length, pages, pageSize, program);
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
		  Logger.getLogger(SketchLoaderCore.class.getPackage().getName()).addAppender(console);
		  Logger.getLogger(SketchLoaderCore.class.getPackage().getName()).setLevel(Level.ERROR);
		  Logger.getRootLogger().addAppender(console);
		  // quiet logger
		  Logger.getRootLogger().setLevel(Level.ERROR);
	}
}
