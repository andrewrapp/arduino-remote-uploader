package com.rapplogic.aru.uploader.wifi;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;
import java.net.UnknownHostException;

public class WifiSocketTest {

	private final Object lock = new Object();
	private StringBuilder stringBuilder = new StringBuilder();
	private String reply;
	
	public WifiSocketTest() throws UnknownHostException, IOException, InterruptedException {
		
		Socket socket = new Socket("192.168.1.115", 1111);
		
		System.out.println("Keep alive " + socket.getKeepAlive());
		System.out.println("Receive buffer size " + socket.getReceiveBufferSize());
		System.out.println("Send buffer size " + socket.getSendBufferSize());
		System.out.println("So linger " + socket.getSoLinger());
		System.out.println("So timeout " + socket.getSoTimeout());
		System.out.println("Tcp nodelay " + socket.getTcpNoDelay());
		
		//socket.setReceiveBufferSize(146808);
		//socket.setSendBufferSize(146808);
		
		
		//socket.setKeepAlive(true);
		// bad sends each char in IPD
//		socket.setTcpNoDelay(true);
		
		//socket.setSendBufferSize(1024);
		
		OutputStream outputStream = socket.getOutputStream();
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
							System.out.print("-->" + ch);						
						}
						
						if (ch == 10) {
							synchronized (lock) {
								System.out.println("Reply is " + stringBuilder.toString());
								lock.notify();
							}
						}
					}
					
					System.out.println("input stream closing");
				} catch (IOException e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
				}
			}
		});
		
		t.setDaemon(true);
		t.start();
		
		// ctrl-c
		//255-->244-->255-->253-->6
		
		Thread.sleep(50);
		
		for (int i = 0; i < 20; i++) {
			sendFakePacket(outputStream);;
			
			synchronized (lock) {
				// choose delay based success times
				System.out.println("Waiting for reply");
				lock.wait(5000);
				System.out.println("reply is " + stringBuilder.toString());
				
				if (!"ok".equals(stringBuilder.toString())) {
					System.out.println("retry");
//					i--;
					continue;
				}
				
				stringBuilder = new StringBuilder();
			}
			
			// needs a delay to get it's ducks in a row before sending again
			Thread.sleep(50);
		}

		Thread.sleep(2000);
	}
	
	public void sendFakePacket(OutputStream outputStream) throws IOException, InterruptedException {
		
		System.out.println("Send packet");
		
		// first char is discarded??
		//outputStream.write(' ');

		String message = "hi there how are you doing today?\r\n";
		byte[] messageB = new byte[10];
		
		for (int i = 0; i < messageB.length; i++) {
			messageB[i] = (byte) 0xff;
		}
		
		// degrades around 46. some kind of race condition perhaps, not exact size where it fails
		// still has failed at 32 but much less so far
//		for (int i = 0; i < 5; i++) {
//			//System.out.println("Writing " + i);
//			//outputStream.write((char) i + 48);
//			//outputStream.write(48);
//			
//			// can send non-ascii
//			//outputStream.write(0xfa);
//			outputStream.write('h');
//			// can send line feeds
//			//outputStream.write('\n');
//			// cr to work also
//			//outputStream.write('\r');
//		}

		
		// not always getting ok back, but radio seems to get always and thinks it's sending
//		outputStream.write(13);
//		outputStream.write(10);

		// must use write(byte[]) or it will be received in multiple IPD commands
		outputStream.write(message.getBytes());
//		outputStream.write(messageB);
		
		outputStream.flush();
	}
	
	public static void main(String[] args) throws UnknownHostException, IOException, InterruptedException {
		new WifiSocketTest();
	}
}
