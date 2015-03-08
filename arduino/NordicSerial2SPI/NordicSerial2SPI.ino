#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"

#define MAGIC_BYTE1 0xef
#define MAGIC_BYTE2 0xac

#define NORDIC_CE 10
#define NORDIC_CS 11

#define TX_FAILURE -1
#define ACK_FAILURE -2
#define START_OVER -3
#define TIMEOUT -4

RF24 radio(NORDIC_CE, NORDIC_CS);

// Radio pipe addresses for the 2 nodes to communicate.
// choose base address
uint64_t baseAddress = 0xca05cade05LL;
const uint64_t pipes[2] = { baseAddress, baseAddress + 1 };

// 32 bytes
uint8_t data[32];
uint8_t ack[32];
uint8_t b;
uint8_t last_byte;

long last_packet;

void dump_buffer(uint8_t arr[], uint8_t len, char context[]) {
    Serial.print(context);
    Serial.print(": ");
  
    for (int i = 0; i < len; i++) {
      Serial.print(arr[i], HEX);
    
      if (i < len -1) {
        Serial.print(",");
      }
    }
  
    Serial.println("");
    Serial.flush();
}

void setup(void) {
  while (!Serial);
  Serial.begin(19200);
  
//  delay(4000);
  
  //printf_begin();
  //printf("Start TX\n\r");  

  radio.begin();

  // doesn't say what the default is
  radio.setPALevel(RF24_PA_MAX);
  
  // try a differnt speed RF24_1MBPS
  radio.setDataRate(RF24_250KBPS);
  
  //pick an arbitrary channel. default is 4c
  radio.setChannel(0x8);
  
  radio.setRetries(3,15);
  
  // write to 0, read from 1    
  radio.openWritingPipe(pipes[0]);
  radio.openReadingPipe(1,pipes[1]);      
  radio.startListening();
  
  // screws up communication
  //radio.printDetails();
}

uint8_t pos = 0;
uint8_t data_pos = 0;
uint8_t length = 0;

bool parsing = false;

int send_packet() {
  radio.stopListening();
            
  int success = 0;
      
  //dump_buffer(data, 32, "Sending");
        
    if (!radio.write(data, 32)) {
      success = TX_FAILURE;         
    } else {
      // wait for ACK
      radio.startListening();      
      
      long start = millis();
      
      // wait for ACK
      bool timeout = false;
      
      while (!radio.available()) {
        if (millis() - start > 3000) {
          Serial.println("Timeout waiting for ACK");
          timeout = true;
          return ACK_FAILURE;
        }
      }
      
      // Unfortunately once radio.available() returns true, it resets back to false, so I need a timeout variable
      bool done = false;
        
      while (!done) {
        done = radio.read(ack, 32);
        
        if (ack[2] == 1) {
          // ok
          Serial.println("OK");
        } else if (ack[2] == 2) {
          Serial.println("ERROR: Start over");
          success = START_OVER;
        } else if (ack[2] == 3) {
          Serial.print("ERROR: Time out");          
          success = TIMEOUT;
        }
        
        //dump_buffer(ack, 32, "ACK");
        //Serial.println(ack[2], HEX);
      } 
      
      // wait for radio to swtich modes
      delay(20); 
              
      radio.startListening();   
      
      return success;
    }
}

void resetPacket() {

}

void reset() {
  pos = 0;
  data_pos = 0;
  parsing = false;
  last_byte = -1;
  
  for (int i = 0; i < 32; i++) {
    data[i] = 0;
  }
}

void loop() {
  while (Serial.available() > 0) {
    b = Serial.read();

    if (!parsing) {
      // look for programming packet

      if (last_byte == MAGIC_BYTE1 && b == MAGIC_BYTE2) {
          //Serial.println("Packet start");
          parsing = true;        
          data[0] = MAGIC_BYTE1;
          data_pos = 1;
      }      
    }
    
    last_byte = b;
    
    if (parsing) {
      //Serial.print("assigning "); Serial.print(b, HEX); Serial.print(" at pos "); Serial.println(data_pos);
      
      data[data_pos] = b;
      last_packet = millis();
     
      if (data_pos == 3) {
        Serial.print("length is "); Serial.println(b, HEX);
        length = b;
      }
     
      if (length > 0 && data_pos + 1 == length) {
        // complete packet

        dump_buffer(data, 32, "sending packet");  
        
        bool sent = false;
        
        // TODO instead of retries send back a resend
        for (int i = 0; i < 10; i++) {
          int response = send_packet();
          if (response == 0) {
            sent = true;
            break; 
          } else if (response == TX_FAILURE || response == ACK_FAILURE) {
            Serial.println("Retrying...");
            delay(100);
          } else {
            break; 
          }
        }  
        
        if (sent) {
          Serial.println("OK");
        } else {
          Serial.println("ERROR: TX failure");        
        }
        
        reset();
      } else {
        data_pos++;       
      }
    }
  }

  if (parsing && millis() - last_packet > 7000) {
    Serial.println("ERROR: Timeout");
    reset();
  }  
}
