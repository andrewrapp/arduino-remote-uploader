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
import java.util.Enumeration;
import java.util.List;
import java.util.TooManyListenersException;

import com.google.common.io.Files;

public class Simple implements SerialPortEventListener{

	private InputStream inputStream;
	private SerialPort serialPort;	
	final String CR = System.getProperty("line.separator");
	final static String port = "/dev/tty.usbmodemfd121";
    private StringBuffer strBuf = new StringBuffer();

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
	
	public void run() throws Exception {
		this.open("/dev/tty.usbmodemfa131");
		write(1);		
		System.out.println("done");
		
		Thread.sleep(60000);
	}
	
	public static void main(String[] args) throws Exception {
		new Simple().run();
	}
}
