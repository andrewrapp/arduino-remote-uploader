package com.rapplogic.sketcher;

import java.util.List;

public class Sketch {
	private List<Page> pages;
	private int size;
	private int bytesPerPage;
	
	public Sketch(int size, List<Page> pages, int bytesPerPage) {
		this.size = size;
		this.pages = pages;
		this.bytesPerPage = bytesPerPage;
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
	
	public Page getLastPage() {
		return this.getPages().get(this.getPages().size() - 1);
	}
}