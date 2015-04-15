package com.rapplogic.aru.uploader.wifi;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.UnknownHostException;
import java.util.Date;
import java.util.Random;
import java.util.concurrent.atomic.AtomicInteger;

public class WifiPing {

	private final Object lock = new Object();
	private StringBuilder stringBuilder = new StringBuilder();

	final static AtomicInteger attempts = new AtomicInteger();
	final static AtomicInteger fails = new AtomicInteger();
	
	private Socket socket = null;	
	
	public WifiPing() throws UnknownHostException, IOException, InterruptedException {
		try {
			ping();			
		} finally {
			if (socket != null && !socket.isClosed()) {
				socket.close();
			}
		}
	}
	
	Random random = new Random();
	
	private byte[] getByteArray(int size) {
		byte[] messageBytes = new byte[size];
		for (int i = 0; i < size - 2; i++) {
			messageBytes[i] = (byte) random.nextInt(256);
		}
		messageBytes[size - 2] = (byte)13;
		messageBytes[size - 1] = (byte)10;
		
		return messageBytes;
	}
	public void ping() throws IOException, InterruptedException {
		System.out.println(new Date() + " Connecting");

		try {
			socket = new Socket();
			socket.connect(new InetSocketAddress("192.168.1.115", 1111), 10000);
			socket.setSoTimeout(10000);
			System.out.println("Connected");
		} catch (IOException e) {
			System.out.println(new Date() + " unable to connect " + e.toString());
			return;
		}
		
		final Socket fSocket = socket;
		
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
					fSocket.close();
				} catch (Exception e) {
					System.out.println(new Date() + " error in read. exiting " + e.toString());
					e.printStackTrace();
				} finally {
					if (fSocket != null && !fSocket.isClosed()) {
						try {
							fSocket.close();
						} catch (IOException e) {
							System.out.println(new Date() + " failed to close socket " + e.toString());
						}
					}
				}
			}
		});
		
		t.setDaemon(true);
		t.start();
		
		StringBuilder messageBuilder = new StringBuilder();
		
		for (int i = 0; i < 32; i++) {
			messageBuilder.append((char)(i + 65));
		}
		
		Thread.sleep(50);

		// send packet every 1 minute. log failures
		while (true) {
			attempts.getAndIncrement();
			
			String message = messageBuilder.toString();
			//String message = "hi there how are you doing today?"; 
			
			try {
//				byte[] messageBytes = (message + "\r\n").getBytes();
				// 45 seems to be max size w/o errors
				sendFakePacket(getByteArray(45), outputStream);
			} catch (IOException e) {
				System.out.println(new Date() + " unable to send packet " + e.toString());
				break;
			}
			
			synchronized (lock) {
				// choose delay based success times
				lock.wait(5000);
				
				if (!"ok".equals(stringBuilder.toString())) {
					fails.getAndIncrement();
					System.out.println(new Date() + " No ack. Fails " + fails.get() + " of " + attempts.get());
				} else {
//					System.out.println(new Date() + " Success");
				}
				
				stringBuilder = new StringBuilder();
			}
			
			Thread.sleep(100);
		}
	}
	
	public void sendFakePacket(byte[] message, OutputStream outputStream) throws IOException, InterruptedException {
		// when arduino resets esp, it doesn't kill the connection so we don't get an i/o exception for a few mins
		// TODO see if connection can be close on arduino prior to reset
		// java.net.SocketException: Broken pipe
		
		// still and issue with esp8266 where if it closes the connection and restarts, we can connect and
		// write data but it ignores it for a while
		
//		System.out.println("Sending message " + message);
		outputStream.write(message);
		outputStream.flush();
	}
	
	public static void main(String[] args) throws UnknownHostException, IOException, InterruptedException {
		while (true) {
			try {
				new WifiPing().ping();	
			} catch (Exception e) {
				e.printStackTrace();
				System.out.println(new Date() + " Unexpected error error: " + e.toString());
			} finally {
				// give it some time
				Thread.sleep(30000);
			}
		}
	}
}
