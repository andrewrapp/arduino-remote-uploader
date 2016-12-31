#!/bin/bash
# unfortunately gotta use java6 on mac due to rxtx issues. available here https://support.apple.com/kb/dl1572?locale=en_US
/System/Library/Frameworks/JavaVM.framework/Versions/1.6/Home/bin/java -d32 -Djava.library.path=. -classpath "*" com.rapplogic.aru.uploader.xbee.XBeeSketchUploader "$@"
