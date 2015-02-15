#include <extEEPROM.h>    //http://github.com/JChristensen/extEEPROM/tree/dev
#include <Streaming.h>    //http://arduiniana.org/libraries/streaming/
#include <Wire.h>         //http://arduino.cc/en/Reference/Wire

// wiring: 1-4 -> gnd
// 5,6 i2c (arduino diecimila analog 4,5)
// 7->gnd (from datasheet but doesn't seem to matter actually)
// 8->vcc

/*
Board	I2C / TWI pins
Uno, Ethernet	A4 (SDA), A5 (SCL)
Mega2560	20 (SDA), 21 (SCL)
Leonardo	2 (SDA), 3 (SCL)
Due	20 (SDA), 21 (SCL), SDA1, SCL1
*/

void setup(void)
{
  Serial.begin(9600);
  while (!Serial);
  
  delay(5000);
  
  extEEPROM myEEPROM(kbits_256, 1, 64);
  byte i2cStat = myEEPROM.begin(twiClock400kHz);
  //byte i2cStat = myEEPROM.begin(twiClock100kHz);
  
  if (i2cStat == 0) {
    Serial.println("ok I guess");

    // error is negative for read byte
    int read = myEEPROM.read(0);
    
    //read a byte
    if (read >= 0) {
      Serial.print("ok read @ addr 0"); Serial.println(read);
    } else {
      Serial.println("fail");
    }
    
    if (myEEPROM.write(0, read + 1) == 0) {
       Serial.print("ok write"); Serial.println(read + 1);
    }

    // error is negative for read byte
    read = myEEPROM.read(0);
    
    //read a byte
    if (read >= 0) {
      Serial.print("ok read"); Serial.println(read);
    } else {
      Serial.println("fail");
    }
    
    int buf_len = 100;
    uint8_t buf[buf_len];
    
    for (int i = 0; i < buf_len; i++) {
      buf[i] = i;
    }
    
    int blockaddr = 1000;
    
    if (myEEPROM.write(blockaddr, buf, buf_len) == 0) {
       Serial.println("ok write block");       
    } else {
      Serial.println("fail write block");       
    }
    
    // zero it
    for (int i = 0; i < buf_len; i++) {
      buf[i] = 0;
    }    
    
    read = myEEPROM.read(blockaddr, buf, buf_len);
    
    if (read == 0) {
       Serial.println("ok read block");        
    } else {
       Serial.print("fail read block. error "); Serial.println(read); 
    }
    
    Serial.print("buf[10] is "); Serial.print(buf[10]);

  } else {
    Serial.println("fail");

  }

}

void loop(void)
{
}


