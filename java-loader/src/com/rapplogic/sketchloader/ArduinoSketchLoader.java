package com.rapplogic.sketchloader;


import gnu.io.CommPortIdentifier;
import gnu.io.PortInUseException;
import gnu.io.SerialPort;
import gnu.io.SerialPortEvent;
import gnu.io.SerialPortEventListener;
import gnu.io.UnsupportedCommOperationException;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.Charset;
import java.util.Enumeration;
import java.util.List;
import java.util.TooManyListenersException;

import com.google.common.collect.Lists;
import com.google.common.io.Files;

public class ArduinoSketchLoader implements SerialPortEventListener {

	final int FIRST_PAGE = 0xd;
	final int LAST_PAGE = 0xf;
	final int PAGE_DATA = 0xa;
	
	final int MAX_PROGRAM_SIZE = 0x20000;
	final int ARDUINO_PAGE_SIZE = 128;
	//final int BAUD_RATE = 115200;
	final int BAUD_RATE = 19200;
	
	private InputStream inputStream;
	private SerialPort serialPort;
    private StringBuffer strBuf = new StringBuffer();
    private Object pageAck = new Object();
    
	public ArduinoSketchLoader() {
		Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
			@Override
			public void run() {
				if (serialPort != null) {
					serialPort.close();					
				}
			}}));
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
    		
    		System.out.println("line: " + ++lineCount + ": " + line);

    		int checksum = 0;
    		
    		if (line.length() > 10) {
	    		int length = Integer.decode("0x" + dataLine.substring(0, 2));
	    		int address = Integer.decode("0x" + dataLine.substring(2, 6));
	    		int type = Integer.decode("0x" + dataLine.substring(6, 8));	    			
    		
	    		checksum += length + address + type;

	    		System.out.println("Length is " + length + ", address is " + Integer.toHexString(address) + ", type is " + Integer.toHexString(type));
	    		
                // Ignore all record types except 00, which is a data record. 
                // Look out for 02 records which set the high order byte of the address space
                if (type == 0) {
                    // Normal data record
                } else if (type == 4 && length == 2 && address == 0 && line.length() > 12) {
                	// Address > 16bit not supported by Arduino so not important
                	throw new RuntimeException("Record type 4 is not implemented");
                } else {
                	System.out.println("Skipped: " + line);
                    continue;
                }
                
                // verify the addr matches our current array position
                if (position > 0 && position != address) {
                	throw new RuntimeException("Expected address of " + position + " but was " + address);
                }
                
                // address will always be last position or we'd have gaps in the array
                position = address;
                
//                System.out.println("Program position is " + position);
                {
                	int i = 8;
	                // data starts at 8 (4th byte) to end minus checksum
	                for (;i < 8 + (length*2); i+=2) {
	                    int b = Integer.decode("0x" + dataLine.substring(i, i+2));	
	                    checksum+= b;
	                    // what we're doing here is simply parsing the data portion of each line and adding to the array
	                    program[position] = b;
	                    position++;
	                }	     
	                
	                System.out.println("Program data: " + toHex(program, position - length, length));
	                
	                // we should be at the checksum position
	                if (i != dataLine.length() - 2) {
	                	throw new RuntimeException("Line length does not match expected length " + length);
	                }
	                
	                int expectedChecksum = Integer.decode("0x" + line.substring(line.length() - 2, line.length()));
	                System.out.println("Expected checksum is " + line.substring(line.length() - 2, line.length()));
	                	                
	                checksum = 0xff - checksum & 0xff;
	                
	                System.out.println("Checksum is " + Integer.toHexString(checksum & 0xff));
	                
	                // wat??
//		                if (checksum != expectedChecksum) {
//		                	throw new RuntimeException("Expected checksum is " + expectedChecksum + " but actual is " + checksum);
//		                }
                }  
    		}
    	}
    	
    	int[] resize = new int[position];
    	System.arraycopy(program, 0, resize, 0, position);
    	
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
    
	public void openSerial(String serialPortName, int speed) throws PortInUseException, IOException, UnsupportedCommOperationException, TooManyListenersException {
		
		CommPortIdentifier commPortIdentifier = findPort(serialPortName);
		
		// initalize serial port
		serialPort = (SerialPort) commPortIdentifier.open("Arduino", 2000);
		inputStream = serialPort.getInputStream();
		serialPort.addEventListener(this);

		// activate the DATA_AVAILABLE notifier
		serialPort.notifyOnDataAvailable(true);

		serialPort.setSerialPortParams(speed, SerialPort.DATABITS_8, 
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
            			   System.out.println("Arduino:<-" + strBuf.toString());
          
            			   if (strBuf.toString().equals("ok")) {
                			   synchronized (pageAck) {
                				   pageAck.notify();
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
		serialPort.getOutputStream().write(i);
	}

	class Sketch {
		private List<Page> pages;
		private int size;
		private int bytesPerPage;
		
		public Sketch(int size, List<Page> pages, int bytesPerPage) {
			this.size = size;
			this.pages = pages;
			this.bytesPerPage = bytesPerPage;
		}

		public List<Page> getPages() {
			return pages;
		}

		public int getSize() {
			return size;
		}

		// slightly redundant
		public int getBytesPerPage() {
			return bytesPerPage;
		}
	}
	
	class Page {
		private int address;
		private int[] data;
		// includes address
		private int[] page;
		private int ordinal;
		
		public Page(int[] program, int offset, int dataLength, int ordinal) {
			super();
			this.address = offset / 2;
			
			int[] data = new int[dataLength];
			System.arraycopy(program, offset, data, 0, dataLength);
			this.data = data;			
			this.page = new int[dataLength + 2];

			// little endian according to avrdude
			page[0] = this.address & 0xff;
			// msb
			page[1] = (this.address >> 8) & 0xff;

			System.arraycopy(data, 0, page, 2, data.length);
			
			this.ordinal = ordinal;
		}
		
		public int getAddress16() {
			return address;
		}
		
		public int[] getData() {
			return data;
		}

		// includes address low/high + data
		public int[] getPage() {
			return page;
		}

		public int getOrdinal() {
			return ordinal;
		}
	}
	
	public Sketch getSketch(String fileName, int pageSize) throws IOException {	
		
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
		
		return new Sketch(program.length, pages, pageSize);
	}
	
//	protected Sketch getSketch(String fileName, int pageSize) throws IOException {
//		return processPages(fileName, pageSize);		
//	}
	
	public void process(String device, String hex) throws Exception {

		int[] program = parseIntelHex(hex);
		Sketch sketch = getSketch(hex, ARDUINO_PAGE_SIZE);	
		
		System.out.println("Program length is " + program.length + ", there are " + sketch.getPages().size() + " pages");
		
		this.openSerial(device, BAUD_RATE);
		
		for (int i = 0; i < sketch.getPages().size(); i++) {
			Page page = sketch.getPages().get(i);
			
			System.out.println("Sending page " + (i + 1) + " of " + sketch.getPages().size() + ", length is " + page.getData().length + ", address is " + Integer.toHexString(page.getAddress16()) + ", page is " + toHex(page.getPage()));

			if (i == 0) {
				write(FIRST_PAGE);
			} else if (i == sketch.getPages().size() - 1) {
				write(LAST_PAGE);
			} else {
				write(PAGE_DATA);
			}
		
//			// only data length, does not include ctrl, len, or addr bytes
			write(page.getData().length);
			
			for (int k = 0; k < page.getPage().length; k++) {
				write(page.getPage()[k] & 0xff);
				// must flush after each write or usb-serial chokes randomly > 90 or so bytes
				serialPort.getOutputStream().flush();
			}
			
			serialPort.getOutputStream().flush();
			
//			System.out.println("Waiting for ack from Arduino");
			
//			// wait for reply before sending more data
			synchronized (pageAck) {
				// TODO timeout
				pageAck.wait();
			}
		}
		
		System.out.println("Java done");
		
		// wait a few secs for leave prog mode reply
		Thread.sleep(5000);
		
		// close port to 
		serialPort.close();
	}
	
	public static void main(String[] args) throws Exception {		
		new ArduinoSketchLoader().process(args[0], args[1]);
	}
}
