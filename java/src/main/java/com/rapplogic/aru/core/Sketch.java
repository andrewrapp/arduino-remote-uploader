package com.rapplogic.aru.core;

import java.util.List;

public class Sketch {
	private List<Page> pages;
	private int size;
	private int bytesPerPage;
	private int[] program;
	
	public Sketch(int size, List<Page> pages, int bytesPerPage, int[] program) {
		this.size = size;
		this.pages = pages;
		this.bytesPerPage = bytesPerPage;
		this.program = program;
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

	public int[] getProgram() {
		return program;
	}

	public Page getLastPage() {
		return this.getPages().get(this.getPages().size() - 1);
	}
}