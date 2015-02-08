package com.rapplogic.sketchloader;


import gnu.io.CommPortIdentifier;
import gnu.io.PortInUseException;
import gnu.io.SerialPort;
import gnu.io.SerialPortEvent;
import gnu.io.SerialPortEventListener;
import gnu.io.UnsupportedCommOperationException;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.Charset;
import java.util.Arrays;
import java.util.Enumeration;
import java.util.List;
import java.util.TooManyListenersException;

import com.google.common.collect.Lists;
import com.google.common.io.Files;

public class Stk500 implements SerialPortEventListener{

	private InputStream inputStream;
	private SerialPort serialPort;
	final String CR = System.getProperty("line.separator");
	final static String port = "/dev/tty.usbmodemfd121";
    private StringBuffer strBuf = new StringBuffer();
    private Object rxNotify = new Object();
    
	int MAX_PROGRAM_SIZE = 0x20000;
	int ARDUINO_BLOB_SIZE = 128;
//	int ARDUINO_BLOB_SIZE = 64;
	
	public Stk500() {
		Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {

			@Override
			public void run() {
				if (serialPort != null) {
					serialPort.close();					
				}
			}}));
	}
	
	public int[] process(String file) throws IOException {
	//	    File hexFile = new File(file);
		
		File hexFile = new File("/var/folders/g1/vflh_srj3gb8zvpx_r5b9phw0000gn/T/build6764070264950839846.tmp/HelloTest.cpp.hex");
	    	
	        // Look at this doc to work out what we need and don't. Max is about 122kb.
	        // https://bluegiga.zendesk.com/entries/42713448--REFERENCE-Updating-BLE11x-firmware-using-UART-DFU
	        
		int[] program = new int[MAX_PROGRAM_SIZE];
		
		// init
		for (int i = 0; i < program.length; i++) {
			program[i] = 0;
		}
		
    	// eg
		//length = 0x10
		// addr = 0000
		// type = 00
		//:100000000C94C7010C94EF010C94EF010C94EF01D8
		//:061860000994F894FFCF8B
		//:00000001FF
		
    	List<String> hex = Files.readLines(hexFile, Charset.forName("UTF-8"));
    	
    	int maxaddress = 0;
		int offset = 0;
		int lineCount = 0;
		int position = 0;

    	for (String line : hex) {

    		String dataLine = line.split(":")[1];
    		
    		System.out.println("line: " + ++lineCount + " is " + line);

    		int checksum = 0;
    		
    		if (line.length() > 10) {
	    		int length = Integer.decode("0x" + dataLine.substring(0, 2));
	    		int addr = Integer.decode("0x" + dataLine.substring(2, 6));
	    		int type = Integer.decode("0x" + dataLine.substring(6, 8));	    			
    		
	    		checksum += length + addr + type;
	    		
	    		// at least one line is 14 length, not the last either
	    		
	    		if (length != 16) {
	    			System.out.println("Length is " + length + " " + line);
	    		}
	    		
//		    		if (ARDUINO_BLOB_SIZE % length != 0) {
//		    			throw new RuntimeException("Length " + length + " is not divisible by ARDUINO_BLOB_SIZE " + ARDUINO_BLOB_SIZE);
//		    		}
	    		
	    		System.out.println("len is " + length + ", addr is " + addr + ", type is " + type);
	    		
                // Ignore all record types except 00, which is a data record. 
                // Look out for 02 records which set the high order byte of the address space
                if (type == 0) {
                    // Normal data record
                } else if (type == 4 && length == 2 && addr == 0 && line.length() > 12) {
                    // Set the offset
                	offset = Integer.decode("0x" + dataLine.substring(8, 12)) << 16;	                	
                    
                	if (offset != 0) {
                        System.out.println("Set offset to" + Integer.toHexString(offset));
                    }
                    continue;
                } else {
                	System.out.println("Skipped: " + line);
                    continue;
                }
                
                // verify the addr matches our current array position
                
                if (position > 0 && position != addr) {
                	throw new RuntimeException("Expected address of " + position + " but was " + addr);
                }
                
                // addr will always be last position or we'd have gaps in the array
                position = offset + addr;
                
                System.out.println("Program position is " + position);
                {
                	// FIXME if there is an offset we'd be including it in the data, oops
                	
                	int i = 8;
	                // data starts at 8 (4th byte) to end minus checksum
	                for (;i < 8 + (length*2); i+=2) {
	                    int datum = Integer.decode("0x" + dataLine.substring(i, i+2));	
	                    
	                    checksum+= datum;
	                    
//		                    System.out.println("Writing " + Integer.toHexString(datum) + " to program at i " + i);	                    
	                    // what we're doing here is simply parsing the data portion of each line and adding to the array
	                    // 
	                    program[position] = datum;
	                    position++;
	                }	                	
	                
	                // we should be at the checksum position
	                if (i != dataLine.length() - 2) {
	                	throw new RuntimeException("Line length does not match expected length " + length);
	                }
	                
	                // Checking the checksum would be a good idea but skipped for now
	                int expectedChecksum = Integer.decode("0x" + line.substring(line.length() - 2, line.length()));
	                System.out.println("Expected checksum is " + line.substring(line.length() - 2, line.length()));
	                	                
	                checksum = checksum & 0xff;
	                checksum = 0xff - checksum;
	                
	                System.out.println("Checksum is " + Integer.toHexString(checksum & 0xff));
	                
	                // wat??
//		                if (checksum != expectedChecksum) {
//		                	throw new RuntimeException("Expected checksum is " + expectedChecksum + " but actual is " + checksum);
//		                }
                }

                System.out.println("Position is " + position + ", maxaddr is " + maxaddress);
                
                // it will always be the last position +16 bytes per line
//	                if (position > maxaddress) {
                	maxaddress = position;
//	                }              
    		}
    	}
    	
    	System.out.println("Program size is " + maxaddress + ", array is " + program.length);
    	
    	int[] resize = new int[position];
    	System.arraycopy(program, 0, resize, 0, position);;
    	
    	return resize;
	}

    public CommPortIdentifier findPort(String port) {
        // parse ports and if the default port is found, initialized the reader
        Enumeration portList = CommPortIdentifier.getPortIdentifiers();
        
        while (portList.hasMoreElements()) {
            
            CommPortIdentifier portId = (CommPortIdentifier) portList.nextElement();
            
            if (portId.getPortType() == CommPortIdentifier.PORT_SERIAL) {

                System.out.println("Found port: " + portId.getName());
                
                if (portId.getName().equals(port)) {
                    System.out.println("Using Port: " + portId.getName());
                    return portId;
                }
            }
        }
        
        throw new RuntimeException("Port not found " + port);
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
    
	public void open(String serialPortName) throws PortInUseException, IOException, UnsupportedCommOperationException, TooManyListenersException {
		
		CommPortIdentifier commPortIdentifier = findPort(serialPortName);
		
		// initalize serial port
		serialPort = (SerialPort) commPortIdentifier.open("Arduino", 2000);
		inputStream = serialPort.getInputStream();
		serialPort.addEventListener(this);

		// activate the DATA_AVAILABLE notifier
		serialPort.notifyOnDataAvailable(true);

		serialPort.setSerialPortParams(9600, SerialPort.DATABITS_8, 
				SerialPort.STOPBITS_1, SerialPort.PARITY_NONE);
		serialPort.setFlowControlMode(SerialPort.FLOWCONTROL_NONE);
		
		Runtime.getRuntime().addShutdownHook(new Thread() {
		    public void run() {
//		        shutdown();
		    }
		});
	}

	public void serialEvent(SerialPortEvent event) {
	    try {
	        handleSerial(event);
	    } catch (Throwable t) {
	        t.printStackTrace();
	    }
	}
	
	protected void handleSerial(SerialPortEvent event) {
       switch (event.getEventType()) {
           case SerialPortEvent.DATA_AVAILABLE:
               // we get here if data has been received
               byte[] readBuffer = new byte[20];
               
               try {
                   // read data
            	   int numBytes = inputStream.read(readBuffer);
            	   
            	   for (int i = 0; i < numBytes; i++) {
            		   //System.out.println("read " + (char) readBuffer[i]);
 
            		   if ((readBuffer[i] != 10 && readBuffer[i] != 13)) {
            			   strBuf.append((char) readBuffer[i]);                            
            		   }
  
            		   //carriage return
            		   if ((int)readBuffer[i] == 10) {         
            			   System.out.println("Arduino out: " + strBuf.toString());
          
            			   if (strBuf.toString().equals("ok")) {
                			   synchronized (rxNotify) {
                				   rxNotify.notify();
                			   }            				   
            			   }

            			   strBuf = new StringBuffer();
            		   }
            	   }
               } catch (Exception e) {
                   throw new RuntimeException("serialEvent error ", e);
               }

               break;
           }
	}
	
	public void write(int i) throws IOException {
		System.out.print(Integer.toHexString(i) + ",");
		serialPort.getOutputStream().write(i);
	}

	static class Page {
		private int address;
//		private List<Integer> data = Lists.newArrayList();
		private int[] data;
		// incl. address
		private int[] page;
		
		public Page(int[] program, int offset, int dataLength) {
			super();
			// address is simple the offset (array index)
			
			// tomatoless divides by 2 pline.addr <- addr / 2; // Address space is 16-bit
			this.address = offset / 2;
			//this.address = offset;
			
			int[] data = new int[dataLength];
			System.arraycopy(program, offset, data, 0, dataLength);
			this.data = data;
			
			page = new int[dataLength + 2];

			// little endian according to avrdude
			page[0] = this.address & 0xff;
			// msb
			page[1] = (this.address >> 8) & 0xff;
			
			// addr msb
//			page[0] = (this.address >> 8) & 0xff;
//			// lsb
//			page[1] = this.address & 0xff;
			
			// copy data onto stk page
			System.arraycopy(data, 0, page, 2, data.length);
		}
		
		public int getAddress() {
			return address;
		}
		
		public int[] getData() {
			return data;
		}

		// address high/low + data
		public int[] getPage() {
			return page;
		}
	}
	
	public List<Page> formatPages(int[] program, int pageSize) {		
		List<Page> pages = Lists.newArrayList();
		
		System.out.println("Program length is " + program.length);
		
		int position = 0;
		
		// write the program to the arduino in chunks of ARDUINO_BLOB_SIZE
		while (position < program.length) {
			
			int length = 0;
			
			if (position + pageSize < program.length) {
				length = pageSize;
			} else {
				length = program.length - position;
			}

			pages.add(new Page(program, position, length));
			
			// index to next position
			position+=length;
		}
		
		return pages;
	}
	
	final int FIRST = 0xd;
	final int LAST = 0xf;
	
	public void run() throws Exception {
		int[] program = process(null);
		
//		List<Page> pages = formatPages(program, ARDUINO_BLOB_SIZE);
		List<Page> pages = formatPages(program, 64);
		
		System.out.println("Program length is " + program.length + ", there are " + pages.size() + " pages");
		
		this.open("/dev/tty.usbmodemfa131");
		
		for (int i = 0; i < pages.size(); i++) {
			Page page = pages.get(i);
			
			System.out.println("Sending page " + (i + 1) + " of " + pages.size() + ", address is " + page.getAddress() + ", data is " + toHex(page.getPage()));
			
			if (i == 0) {
				write(0xd);
			} else if (i == pages.size() - 1) {
				write(0xf);
			} else {
				write(0);
			}
		
//			// packet len = ctrl + len + addr high/low + data
			write(page.getPage().length + 2);
			
			for (int k = 0; k < page.getPage().length; k++) {
				write(page.getPage()[k] & 0xff);	
			}
			
			serialPort.getOutputStream().flush();
			
			System.out.println("\nwaiting for ok");
//			// wait for reply
			synchronized (rxNotify) {
				rxNotify.wait();
			}
			
//			if (i == 0) {
//				break;
//			}
		}
		
		System.out.println("Java done");
		
		Thread.sleep(60000);
	}
	
	public static void main(String[] args) throws Exception {
		new Stk500().run();
	}
}
