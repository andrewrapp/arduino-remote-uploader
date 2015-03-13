package com.rapplogic.aru.uploader.serial;


import gnu.io.CommPortIdentifier;
import gnu.io.PortInUseException;
import gnu.io.SerialPort;
import gnu.io.SerialPortEvent;
import gnu.io.SerialPortEventListener;
import gnu.io.UnsupportedCommOperationException;

import java.io.IOException;
import java.io.InputStream;
import java.util.Enumeration;
import java.util.TooManyListenersException;

import com.rapplogic.aru.core.Page;
import com.rapplogic.aru.core.Sketch;
import com.rapplogic.aru.uploader.SketchUploader;

/**
 * Loads a sketch from host filesystem onto an Arduino via an Arduino Leonardo, running the SerialSketcher sketch
 * 
 * @author andrew
 *
 */
public class SerialSketchLoader extends SketchUploader implements SerialPortEventListener {

	final int FIRST_PAGE = 0xd;
	final int LAST_PAGE = 0xf;
	final int PAGE_DATA = 0xa;
	
	//final int BAUD_RATE = 115200;
	// usb-serial speed. needs to match the sketch of course
	final int BAUD_RATE = 19200;
	
	private InputStream inputStream;
	private SerialPort serialPort;
    private Object pageAck = new Object();
    private StringBuffer strBuf = new StringBuffer();
    
	public SerialSketchLoader() {
		Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
			@Override
			public void run() {
				if (serialPort != null) {
					serialPort.close();					
				}
			}}));
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
	
	public InputStream getInputStream() {
		return inputStream;
	}

	public void write(int i) throws IOException {
		serialPort.getOutputStream().write(i);
		// must flush after each write or usb-serial chokes randomly > 90 or so bytes
		serialPort.getOutputStream().flush();		
	}
	
	public void process(String device, String hex) throws Exception {

		int[] program = parseIntelHex(hex);
		Sketch sketch = parseSketchFromIntelHex(hex, ARDUINO_PAGE_SIZE);	
		
		//System.out.println("Sending sketch to Arduino via serial. Program length is " + program.length + ", there are " + sketch.getPages().size() + " pages");
		
		this.openSerial(device, BAUD_RATE);
		
		for (int i = 0; i < sketch.getPages().size(); i++) {
			Page page = sketch.getPages().get(i);
			
			System.out.println("Sending page " + (i + 1) + " of " + sketch.getPages().size() + ", length is " + page.getData().length + ", address is " + Integer.toHexString(page.getBootloaderAddress16()) + ", page is " + toHex(page.getPage()));

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
		
		// close port to trigger IOException on blocking read call and exit jvm
		serialPort.close();
	}
	
	public SerialPort getSerialPort() {
		return serialPort;
	}

	// of course you can't use the arduino serial monitor with this since it needs exclusive access to the usb-serial port
	public static void main(String[] args) throws Exception {		
		new SerialSketchLoader().process(args[0], args[1]);
	}
}
