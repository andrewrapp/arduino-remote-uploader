#!/bin/bash

/System/Library/Frameworks/JavaVM.framework/Versions/1.6/Home/bin/java -d32 -Djava.library.path=. -classpath "*" com.rapplogic.aru.uploader.xbee.XBeeSketchUploader "$@"
