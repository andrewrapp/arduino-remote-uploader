package com.rapplogic.aru.uploader.wifi;

import java.io.IOException;
import java.io.InputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.UnknownHostException;

public class SocketServerTest {
	
	public SocketServerTest() throws UnknownHostException, IOException, InterruptedException {
		
		ServerSocket serverSocket = new ServerSocket(9000);
		Socket socket = serverSocket.accept();

		System.out.println("Keep alive " + socket.getKeepAlive());
		System.out.println("Receive buffer size " + socket.getReceiveBufferSize());
		System.out.println("Send buffer size " + socket.getSendBufferSize());
		System.out.println("So linger " + socket.getSoLinger());
		System.out.println("So timeout " + socket.getSoTimeout());
		System.out.println("Tcp nodelay " + socket.getTcpNoDelay());
		
		final InputStream inputStream = socket.getInputStream();
		
		Thread t = new Thread(new Runnable() {
			
			@Override
			public void run() {
				int ch = 0;
				
				try {
					while ((ch = inputStream.read()) > -1) {
						if (ch > 32 && ch < 127) {
							System.out.print("-->" + (char)ch);
						} else {
							System.out.print("-->" + ch);						
						}
					}
					
					System.out.println("input stream closing");
				} catch (IOException e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
				}
			}
		});
		
		Thread.sleep(1000);
		t.start();
	}
	
	public static void main(String[] args) throws UnknownHostException, IOException, InterruptedException {
		new SocketServerTest();
	}
}
