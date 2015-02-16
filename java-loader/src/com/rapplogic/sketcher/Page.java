package com.rapplogic.sketcher;

public class Page {
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
	
	// bootloader address is real / 2
	public int getBootloaderAddress16() {
		return address;
	}

	public int getRealAddress16() {
		return address * 2;
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