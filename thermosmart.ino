#include <JeeLib.h>
#include <Ports.h>
#include <PortsBMP085.h>
#include <PortsLCD.h>
#include <PortsSHT11.h>
#include <RF12.h>
#include <RF12sio.h>

#include <Arduino.h>
#include <RF12.h>

#define CRC_POLY 0x31
#define MAX_SENSORS 8
#define DEBUG_OFF

struct Sensor {
  byte sensorId;
  byte temp;
  byte decimalTemp;
  byte hygro;
  byte resetFlag;
  byte weakBatt;
};

void printSensor(Sensor sensor);

Sensor sensors[MAX_SENSORS];

void setup() {
  rf12_initialize(1, RF12_868MHZ, 0xd4);
  
  // Overide settings for RFM01/IT+ compliance
  rf12_initialize_overide_ITP();
  
  Serial.begin(9600);
  #ifdef DEBUG
  Serial.println("Init OK");
  #endif
}

/*
 * The loop read the packets received by the RF12
 * and try to decode it with the TX29 protocol.
 * If it succeed, it stores the result into
 * the sensors array.
 * 
 * Then, if there's something on the serial line,
 * it print all the data contained in the array
 * then clear the sensors array.
 */
void loop() {
  if (rf12_recvDone()) {
    //we receive a packet, let's check if it's an IT+ one
    if (ITPlusFrame && CheckITPlusCRC()) {
      int entryNum = ReadITPlusValue();

      printSensor(sensors[entryNum]);
      Serial.println();
    }
  }
  if (Serial.available() > 0) {
    //emptying the buffer
    Serial.read();
    
    //printing all temperatures
    boolean first = true;
    for (int i = 0 ; i < MAX_SENSORS ; i++) {
      if (sensors[i].sensorId != 0) {
        if (!first) {
          Serial.print("|"); //print sensors delimiter
        }
        first = false;

        printSensor(sensors[i]);
        
        //data retrieved, erase the sensor
        sensors[i].sensorId = 0;
      }
    }
    Serial.println();
  }
}

/*
 * Try to decode the CRC from the packet to check
 * if it's a TX29 one.
 * 
 * Protocol : http://fredboboss.free.fr/tx29/
 * Implementation : http://gcrnet.net/node/32
 */
boolean CheckITPlusCRC() {
  byte nbBytes = 5;
  byte reg = 0;
  byte curByte, curbit, bitmask;
  byte do_xor;
  
  while (nbBytes != 0) {
    curByte = rf12_buf[5-nbBytes];
    nbBytes--;
    bitmask = 0b10000000;
    while (bitmask != 0) {
      curbit = ((curByte & bitmask) == 0) ? 0 : 1;
      bitmask >>= 1;
      do_xor = (reg & 0x80);
      reg <<=1;
      reg |= curbit;
      if (do_xor) {
        reg ^= CRC_POLY;
      }
    }
  }
  
  return (reg == 0);
}

/*
 * The CRC has been check, this method decode the TX29
 * packet to extract the data, then store it into the
 * sensors array.
 */ 
int ReadITPlusValue() {
  byte temp, decimalTemp, sensorId, resetFlag, hygro, weakBatt;
  int pos = -1;
  
  sensorId = (((rf12_buf[0] & 0x0f) << 4) + ((rf12_buf[1] & 0xf0) >> 4)) & 0xfc;
  // Reset flag is stored as bit #6 in sensorID.
  resetFlag = (rf12_buf[1] & 0b00100000) << 1;
  temp = (((rf12_buf[1] & 0x0f) * 10) + ((rf12_buf[2] & 0xf0) >> 4));
  decimalTemp = rf12_buf[2] & 0x0f;

  // IT+ add a 40° offset to temp, so < 40 means negative
  if (temp >= 40) {
    temp -= 40;
  } else {
    if (decimalTemp == 0) {
      temp = 40 - temp;
    } else {
      temp = 39 - temp;
      decimalTemp = 10 - decimalTemp;
    }
    // Sign bit is stored into bit #7 of temperature. 
    temp |= 0b10000000;
  }
  
  //weak battery indicator is the first bit, the rest is hygro
  weakBatt = rf12_buf[3] & 0x80;
  hygro = rf12_buf[3] & 0x7f;
  
  //check the Sensors array to find a empty space
  for (int i = 0 ; i < MAX_SENSORS && pos < 0 ; i++) {
    if (sensors[i].sensorId == 0 || sensors[i].sensorId == sensorId) {
      //that's a free space, store values in it
      pos = i;
      sensors[i].sensorId = sensorId;
      sensors[i].temp = temp;
      sensors[i].decimalTemp = decimalTemp;
      sensors[i].hygro = hygro;
      sensors[i].resetFlag = resetFlag;
      sensors[i].weakBatt = weakBatt;
    }
  }

  return pos;
}

/*
 * Print a number into hex format
 */
void printHex(byte data) {
  if (data < 16) Serial.print('0');
  Serial.print(data, HEX);
}

#ifdef DEBUG
void printSensor(Sensor sensor) {
  Serial.print("Id: "); printHex(sensor.sensorId);
  if (sensor.resetFlag)
    Serial.print(" R");
  else
    Serial.print("  ");

  if (sensor.weakBatt)
    Serial.print("B");
  else
    Serial.print(" ");

  Serial.print(" Temp: ");
  if (sensor.temp & 0b10000000)
    Serial.print("-");
  if (sensor.temp < 10) Serial.print("0");
  Serial.print(sensor.temp & 0x7f, DEC); Serial.print("."); Serial.print(sensor.decimalTemp, DEC);
  if (sensor.hygro != 106) {
    Serial.print(" Hygro: "); Serial.print(sensor.hygro, DEC); Serial.print("%");
  }
}
#else
void printSensor(Sensor sensor) {
  printHex(sensor.sensorId);
  
  Serial.print(",");

  //first bit for negative sign
  if (sensor.temp & 0b10000000) Serial.print("-");
  Serial.print(sensor.temp & 0x7f, DEC);
  Serial.print(".");
  Serial.print(sensor.decimalTemp, DEC);

  Serial.print(",");

  if (sensor.hygro != 106) {
    Serial.print(sensor.hygro, DEC);
  }

  Serial.print(",");

  if (sensor.resetFlag)
    Serial.print("R");
  else
    Serial.print("");

  Serial.print(",");

  if (sensor.weakBatt)
    Serial.print("B");
  else
    Serial.print("");
}
#endif

