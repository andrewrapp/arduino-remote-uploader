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

import com.rapplogic.aru.uploader.SketchUploader;

/**
 * Provides serial port access
 * 
 * @author andrew
 *
 */
public abstract class SerialSketchUploader extends SketchUploader implements SerialPortEventListener {

	private InputStream inputStream;
	private SerialPort serialPort;
    private Object pageAck = new Object();
    private StringBuffer strBuf = new StringBuffer();
    
	public SerialSketchUploader() {
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

                //System.out.println("Found port: " + portId.getName());
                
                if (portId.getName().equals(port)) {
                    //System.out.println("Using Port: " + portId.getName());
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
	
	public SerialPort getSerialPort() {
		return serialPort;
	}
}
