/**
 * Copyright (c) 2015 Andrew Rapp. All rights reserved.
 *
 * This file is part of arduino-remote-uploader
 *
 * arduino-remote-uploader is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * arduino-remote-uploader is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with arduino-remote-uploader.  If not, see <http://www.gnu.org/licenses/>.
 */

package com.rapplogic.aru.uploader;

import java.io.IOException;
import java.util.Arrays;
import java.util.Map;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

import org.apache.log4j.Logger;

import com.rapplogic.aru.core.Page;
import com.rapplogic.aru.core.Sketch;
import com.rapplogic.aru.core.SketchCore;

/**
 * Defines framework for uploading sketch to remote
 * 
 * @author andrew
 *
 */
public abstract class SketchUploader extends SketchCore {
	
	final Logger log = Logger.getLogger(SketchUploader.class);
	
	public final int MAGIC_BYTE1 = 0xef; // 239
	public final int MAGIC_BYTE2 = 0xac; // 172
	// make enum
	public final int CONTROL_PROG_REQUEST = 0x10;
	public final int CONTROL_WRITE_EEPROM = 0x20;
	// somewhat redundant
	public final int CONTROL_START_FLASH = 0x40;
	// tell what version of uploader is running
	public final int CONTROL_VERSION_QUERY = 0x80;

	// releases with non-backwards compatible change to protocol should rev the version number 
	public final int VERSION = 1;
	
	// response/error codes must sync the arduino header file
	public final int OK = 1;
	public final int START_OVER = 2;
	public final int TIMEOUT = 3;
	public final int RETRY = 0xff;
	public final int FLASH_ERROR = 4;
	public final int EEPROM_ERROR = 5;
	public final int EEPROM_WRITE_ERROR = 6;
	public final int EEPROM_READ_ERROR = 7;
	// serial lines not connected or reset pin not connected
	public final int NOBOOTLOADER_ERROR = 8;
	public final int VERIFY_PAGE_ERROR = 9;
	public final int ADDRESS_SKIP_ERROR = 0xa;
	
	private boolean verbose;
	//private boolean programInterrupt;
	
	private BlockingQueue<int[]> ackQueue = new LinkedBlockingQueue<int[]>();
//	private final Thread main = Thread.currentThread();

	private CliOptions cliOptions = new CliOptions();
	
	public SketchUploader() {

	}
	
//	protected Thread getMainThread() {
//		return main;
//	}
	
	/**
	 * Add a reply to the queue
	 */
	protected void addReply(int[] reply) {
		ackQueue.add(reply);
	}
	
	/**
	 * Waits up to timeoutMillis for reply. if id does not match it will wait some more up to the timeout
	 * 
	 * @param ackTimeoutMillis
	 * @param id
	 * @throws NoAckException 
	 * @throws InterruptedException 
	 * @throws StartOverException 
	 */
	protected void waitForAck(final int ackTimeoutMillis, int id) throws NoAckException, InterruptedException, StartOverException {
		long start = System.currentTimeMillis();
		long timeLeftMillis = ackTimeoutMillis;
		
		while (timeLeftMillis > 0) {
			
			boolean interrupted = false;
			
			if (Thread.currentThread().interrupted()) {
				interrupted = true;
			}
			
			int[] reply = null;
			
			try {		
				reply = ackQueue.poll(timeLeftMillis, TimeUnit.MILLISECONDS);			
				// calc how much more time to wait
				timeLeftMillis = ackTimeoutMillis - (System.currentTimeMillis() - start);
			} catch (InterruptedException e) {
				interrupted = true;
			}
			
			if (interrupted) {
//				if (programInterrupt) {
//					programInterrupt = false;
//					// reset
//					throw new NoAckException("Interrupted while waiting for ACK after " + (System.currentTimeMillis() - start) + "ms");					
//				}
				
				throw new InterruptedException();
			}
			
			if (reply == null) {
				//System.out.println("Timeout waiting for reply. timeleft is now " + timeLeftMillis);
			} else {

				switch (reply[2]) {
				case OK:
					int packetId = getPacketId(reply);
					
					if (packetId != id) {
						// ack id does not match tx id
						// if the transport is configured for retries we can get multiple acks. in this case we got a late ack for the previous page or it sent multiple acks
						// if it's negative that's just fine
						if (verbose) {
							System.out.println("Received ack for id " + packetId + " but expected id " + id + ".. ignoring and waiting for " + timeLeftMillis + "ms");							
						}
						
						// wait some more
						continue;
					}
					
					// match
					return;
				case START_OVER:
					throw new StartOverException("Upload failed: arduino said to start over");
				case TIMEOUT:
					throw new StartOverException("Upload failed: arduino sent a timeout reply.. start over");
				case RETRY:
					// fictitious reply. does not come from arduino
					throw new NoAckException("Received RETRY ack");
				case FLASH_ERROR:
				case EEPROM_ERROR:
				case EEPROM_WRITE_ERROR:
				case EEPROM_READ_ERROR:
				case NOBOOTLOADER_ERROR:
				case VERIFY_PAGE_ERROR:
				case ADDRESS_SKIP_ERROR:					
					// TODO handle specific errors
					throw new StartOverException("Upload failed: error code " + reply[2]);
				default:
					throw new StartOverException("Unexpected response code from arduino: " + reply);						
				}				
			}			
		}
		
		throw new NoAckException("No ACK from transport device after " + ackTimeoutMillis + "ms");		
	}
	
	public int[] getStartHeader(int sizeInBytes, int numPages, int bytesPerPage, int timeout, int version) {
		int[] header =  new int[] { 
				MAGIC_BYTE1, 
				MAGIC_BYTE2, 
				CONTROL_PROG_REQUEST, 
				-1, //length set below
				(sizeInBytes >> 8) & 0xff, 
				sizeInBytes & 0xff, 
				(numPages >> 8) & 0xff, 
				numPages & 0xff,
				bytesPerPage,
				timeout & 0xff,
				version
		};
		
		header[3] = header.length;
		
		return header;
	}
	
	// TODO consider adding retry bit to header
	// TODO consider sending force reset bit to header
	
	// NOTE if header size is ever changed must also change PROG_DATA_OFFSET in library
	// xbee has error detection built-in but other protocols may need a checksum
	private int[] getHeader(int controlByte, int addressOrSize, int dataLength) {
		int[] header = new int[] {
				MAGIC_BYTE1, 
				MAGIC_BYTE2, 
				controlByte, 
				-1, //set below
				(addressOrSize >> 8) & 0xff, 
				addressOrSize & 0xff
		};
		
		header[3] = dataLength + header.length;
		
		return header;
	}

	protected int getPacketId(int[] reply) {
		return (reply[3] << 8) + reply[4];
	}
	
	public int[] getProgramPageHeader(int address16, int dataLength) {
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

	protected abstract void open(Map<String,Object> context) throws Exception;
	protected abstract void writeData(int[] data, Map<String,Object> context) throws Exception;
	/**
	 * 
	 * @param timeout
	 * @param id how we know we got the ack for this packet and not a different packet
	 * @throws NoAckException
	 * @throws Exception
	 */
//	protected abstract void waitForAck(int timeout, int id) throws NoAckException, Exception;
	protected abstract void close() throws Exception;
	protected abstract String getName();
	
	/**
	 * 
	 * @param file
	 * @param pageSize
	 * @param ackTimeoutMillis how long we wait for an ack before retrying
	 * @param arduinoTimeoutSec how long before arduino resets after no activity. value of zero will indicates no timeout
	 * @param retriesPerPacket how many times to retry sending a page before giving up
	 * @param verbose
	 * @param context
	 * @throws IOException
	 * @throws StartOverException 
	 */
	public void process(String file, int pageSize, final int ackTimeoutMillis, int arduinoTimeoutSec, int retriesPerPacket, int delayBetweenRetriesMillis, final boolean verbose, final Map<String,Object> context) throws IOException {
		// page size is max packet size for the radio
		final Sketch sketch = parseSketchFromIntelHex(file, pageSize);
			
		// was trying to keep in state-less but this is needed for the rxtx async input
		this.verbose = verbose;
		
		context.put("verbose", verbose);
		
		int retries = 0;
		
		try {
			open(context);
			
			long start = System.currentTimeMillis();
			final int[] startHeader = getStartHeader(sketch.getSize(), sketch.getPages().size(), sketch.getBytesPerPage(), arduinoTimeoutSec, VERSION);
				
			System.out.println("Sending sketch to " + getName() + " radio, size " + sketch.getSize() + " bytes, md5 " + getMd5(sketch.getProgram()) + ", number of packets " + sketch.getPages().size() + ", and " + sketch.getBytesPerPage() + " bytes per packet, header " + toHex(startHeader));
			
			Retryer first = new Retryer(retriesPerPacket, delayBetweenRetriesMillis, "start packet") {
				@Override
				public void send() throws Exception {			
					writeData(startHeader, context);
					waitForAck(ackTimeoutMillis, sketch.getSize());					
				}
			};
			
			retries+= first.sendWithRetries();
			
			for (final Page page : sketch.getPages()) {				
				// make sure we do a timely exit on a kill signal
				if (Thread.currentThread().isInterrupted()) {
//					if (programInterrupt) {
//						programInterrupt = false;
//						Thread.currentThread().interrupted();
//					} else {
						throw new InterruptedException();						
//					}
				}
								
				Retryer retry = new Retryer(retriesPerPacket,  delayBetweenRetriesMillis, "page " + (page.getOrdinal() + 1) + " of " + sketch.getPages().size()) {
					@Override
					public void send() throws NoAckException, InterruptedException, StartOverException {		
						try {
							final int[] data = combine(getProgramPageHeader(page.getRealAddress16(), page.getData().length), page.getData());

							if (verbose) {
								System.out.println("Sending page " + (page.getOrdinal() + 1) + " of " + sketch.getPages().size() + ", with address " + page.getRealAddress16() + ", length " + data.length + ", packet " + toHex(data));
//								System.out.println("Data " + toHex(page.getData()));
							} else {
								System.out.print(".");
								
								if (page.getOrdinal() > 0 && page.getOrdinal() % 80 == 0) {
									System.out.println("");
								}
							}
							
							writeData(data, context);					
						} catch (Exception e) {
							throw new RuntimeException("Unexpected error at page " + (page.getOrdinal() + 1) + " of " + sketch.getPages().size(), e);
						}
						
						// don't send next page until this one is processed or we will overflow the buffer
						waitForAck(ackTimeoutMillis, page.getRealAddress16());				
					}
				};
				
				retries+= retry.sendWithRetries();
			}

			if (!verbose) {
				System.out.println("");
			}

			final int[] flash = getFlashStartHeader(sketch.getSize());
			
			if (verbose) {
				System.out.println("Sending flash packet " + toHex(flash));				
			}
			
			Retryer last = new Retryer(retriesPerPacket, delayBetweenRetriesMillis, "flash start") {
				@Override
				public void send() throws Exception {			
					writeData(flash, context);
					waitForAck(ackTimeoutMillis, sketch.getSize());						
				}
			};
			
			retries+= last.sendWithRetries();

			System.out.println("Successfully flashed remote Arduino in " + (System.currentTimeMillis() - start) / 1000 + "s, with " + retries + " retries");
		} catch (InterruptedException e) {
			// kill signal
			System.out.println("Interrupted during programming.. exiting");
		} catch (StartOverException e) {
			System.err.println("Flash failed " + e.toString());
			log.warn("Start over " + e.getMessage());
		} catch (Exception e) {
			log.error("Unexpected error", e);
			System.err.println("Unexpected error " + e.toString());
			e.printStackTrace();
		} finally {
			try {
				close();
			} catch (Exception e) {}
		}
	}
	
	public static class NoAckException extends Exception {
		public NoAckException(String arg0) {
			super(arg0);
		}
	}
	
	public static class StartOverException extends Exception {
		public StartOverException(String arg0) {
			super(arg0);
		}
	}	
	
	static abstract class Retryer {
		private int retries;
		private int delayBetweenRetriesMillis;
		private String context;
		

		public Retryer(int retries, int delayBetweenRetriesMillis, String context) {
			if (retries <= 0) {
				throw new IllegalArgumentException("Retries must be >= 1");
			}
			
			this.retries = retries;
			this.delayBetweenRetriesMillis = delayBetweenRetriesMillis;
			this.context = context;
		}
	
		public int sendWithRetries() throws StartOverException, InterruptedException {
			for (int i = 0 ;i < retries; i++) {
				try {
					// reset
					send();
					return i;
				} catch (NoAckException e) {
					System.out.println("\nFailed to deliver packet [" + context + "] on attempt " + (i + 1) + ", reason " + e.getMessage() + ".. retrying");
					
					if (i + 1 == retries) {
						throw new RuntimeException("Failed to send after " + (i + 1) + " attempts");
					}
					
					Thread.sleep(delayBetweenRetriesMillis);
					
					continue;
				} catch (StartOverException e) {
					throw e;
				} catch (InterruptedException e) {
					throw e;
				} catch (Exception e) {
					throw new RuntimeException("Unable to retry due to unexpected exception", e);
				}
			}
			
			// only can get here if retries is <= 0 but compiler doesn't get that it can't happen
			throw new RuntimeException();
		}
		
		public abstract void send() throws NoAckException, InterruptedException, Exception;
	}

	public boolean isVerbose() {
		return verbose;
	}
	
	public CliOptions getCliOptions() {
		return cliOptions;
	}

	public void interrupt() {
//		if (Thread.currentThread() != getMainThread()) {
//			programInterrupt = true;
//			getMainThread().interrupt();
//		}
		// else throw illegal
	}
}
