package com.rapplogic.aru.uploader.wifi;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;
import java.net.UnknownHostException;
import java.util.Date;
import java.util.concurrent.atomic.AtomicInteger;

public class WifiPing {

	private final Object lock = new Object();
	private StringBuilder stringBuilder = new StringBuilder();

	final static AtomicInteger attempts = new AtomicInteger();
	final static AtomicInteger fails = new AtomicInteger();
	
	public WifiPing() throws UnknownHostException, IOException, InterruptedException {
		
		System.out.println(new Date() + " Connecting");
		final Socket socket = new Socket("192.168.1.115", 1111);
		
		final OutputStream outputStream = socket.getOutputStream();
		final InputStream inputStream = socket.getInputStream();
		
		Thread t = new Thread(new Runnable() {
			
			@Override
			public void run() {
				int ch = 0;
				
				// reply always terminated with 13,10
				try {
					while ((ch = inputStream.read()) > -1) {
						if (ch > 32 && ch < 127) {
							stringBuilder.append((char)ch);
							//System.out.print("-->" + (char)ch);
						} else {
							//System.out.print("-->" + ch);						
						}
						
						if (ch == 10) {
							synchronized (lock) {
								lock.notify();
							}
						}
					}
					
					System.out.println(new Date() + " end of input stream reached. closing socket");
					// close socket to trigger io exception in main thread
					socket.close();
				} catch (IOException e) {
					System.out.println(new Date() + " socket error in read " + e.toString());
					e.printStackTrace();
				}
			}
		});
		
		t.setDaemon(true);
		t.start();
		
		Thread.sleep(50);

		// send packet every 1 minute. log failures
		while (true) {
			attempts.getAndIncrement();
			sendFakePacket(outputStream);;
			
			synchronized (lock) {
				// choose delay based success times
				lock.wait(5000);
				
				if (!"ok".equals(stringBuilder.toString())) {
					fails.getAndIncrement();
					System.out.println(new Date() + " No ack. Fails " + fails.get() + " of " + attempts.get());
				}
				
				stringBuilder = new StringBuilder();
			}
			
			Thread.sleep(3000);
		}
	}
	
	public void sendFakePacket(OutputStream outputStream) throws IOException, InterruptedException {
		String message = "hi there how are you doing today?\r\n";
		
		// when arduino resets esp, it doesn't kill the connection so we don't get an i/o exception for a few mins
		// TODO see if connection can be close on arduino prior to reset
		// java.net.SocketException: Broken pipe
		
		// still and issue with esp8266 where if it closes the connection and restarts, we can connect and
		// write data but it ignores it for a while
		outputStream.write(message.getBytes());
		outputStream.flush();
	}
	
	public static void main(String[] args) throws UnknownHostException, IOException, InterruptedException {
		while (true) {
			try {
				new WifiPing();			
			} catch (IOException e) {
				System.out.println(new Date() + " IO error: " + e.toString());
				// give it some time
				Thread.sleep(30000);
			}			
		}
	}
}
