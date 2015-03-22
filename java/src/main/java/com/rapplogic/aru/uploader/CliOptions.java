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

import java.util.Map;

import org.apache.commons.cli.CommandLine;
import org.apache.commons.cli.CommandLineParser;
import org.apache.commons.cli.HelpFormatter;
import org.apache.commons.cli.OptionBuilder;
import org.apache.commons.cli.Options;
import org.apache.commons.cli.ParseException;
import org.apache.commons.cli.PosixParser;

import com.google.common.collect.Maps;

public class CliOptions {

	// cli doesn't allow single dashes in args which is insane!
	public final static String sketch = "sketch";
	public final static String verboseArg = "verbose";
	public final static String ackTimeoutMillisArg = "ack-timeout-ms";
	public final static String arduinoTimeoutArg = "arduino-timeout";
	public final static String retriesPerPacketArg = "retries";
	public final static String delayBetweenRetriesMillisArg = "retry-delay-ms";
	// TODO total timeout
	
	final private Options options = new Options();
	private CommandLine commandLine;
	
	private Map<String, String> defaults = Maps.newHashMap();
	
	public CliOptions() {
		options.addOption(OptionBuilder
				.withLongOpt(sketch)
				.hasArg()
				.isRequired(true)
				.withDescription("Path to compiled sketch (compiled by Arduino IDE). Required")
				.create("s"));
		
		defaults.put(ackTimeoutMillisArg, "5000");
		
		options.addOption(
				OptionBuilder
				.withLongOpt(ackTimeoutMillisArg)
				.hasArg()
				.withType(Number.class)
				.isRequired(false)
				.withDescription("How long to wait for ACK from Arduino before retrying (in milliseconds). Default is " + defaults.get(ackTimeoutMillisArg))
				.create("c"));

		defaults.put(arduinoTimeoutArg, "60");
		
		options.addOption(
				OptionBuilder
				.withLongOpt(arduinoTimeoutArg)
				.hasArg()
				.withType(Number.class)
				.isRequired(false)
				.withDescription("How long Arduino waits for a command before it exits programming mode (in seconds). Default is " + defaults.get(arduinoTimeoutArg) + ". Note: This is not the time that programming must complete in, only the max time between commands")
				.create("a"));
		
		defaults.put(retriesPerPacketArg, "10");
		
		options.addOption(
				OptionBuilder
				.withLongOpt(retriesPerPacketArg)
				.hasArg()
				.withType(Number.class)
				.isRequired(false)
				.withDescription("How many times a command is retried before giving up. Default is " + defaults.get(retriesPerPacketArg))
				.create("r"));		
		
		defaults.put(delayBetweenRetriesMillisArg, "250");
		
		options.addOption(
				OptionBuilder
				.withLongOpt(delayBetweenRetriesMillisArg)
				.hasArg()
				.withType(Number.class)
				.isRequired(false)
				.withDescription("Delay before retrying to send a packet (in milliseconds). Default is " + defaults.get(delayBetweenRetriesMillisArg))
				.create("d"));
		
		options.addOption(
				OptionBuilder
				.withLongOpt(verboseArg)
				.isRequired(false)
				.withDescription("Show debug output")
				.create("d"));		
		
		options.addOption(
				OptionBuilder
				.withLongOpt("help")
				.isRequired(false)
				.withDescription("Show help")
				.create("h"));				

		//options.addOption("h", "help", false, "Show help");
	}

	private void printHelp() {
		HelpFormatter formatter = new HelpFormatter();
		formatter.printHelp("uploader", this.getOptions());
	}
	
	public Integer getIntegerOption(String arg) throws ParseException {
		String value = commandLine.getOptionValue(arg, defaults.get(arg));
		
		try {
			return Integer.parseInt(value);
		} catch (NumberFormatException e) {
			throw new ParseException("Invalid option value for " + arg);
		}
	}
	
	public CommandLine parse(String[] args) {
		CommandLineParser parser = new PosixParser();
		
		try {
			commandLine = parser.parse(this.getOptions(), args);	
			
			if (commandLine.hasOption('h')) {
				printHelp();
				return null;
			}
			
			return commandLine;
		} catch (ParseException e) {
			HelpFormatter formatter = new HelpFormatter();
			formatter.printHelp("uploader", this.getOptions());
		}
		
		return null;
	}
	
	public Options getOptions() {
		return options;
	}
}
