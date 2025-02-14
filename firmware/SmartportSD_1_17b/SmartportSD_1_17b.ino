//*****************************************************************************
//
// Apple //c Smartport Compact Flash adapter
// Written by Robert Justice  email: rjustice(at)internode.on.net
// Ported to Arduino UNO with SD Card adapter by Andrea Ottaviani email: andrea.ottaviani.69(at)gmail.com
//
// 1.00 - basic read and write working for one partition
// 1.01 - add support for 4 partions, assume no other smartport devices on bus
// 1.02 - add correct handling of device ids, should work with other smartport devices
//        before this one in the drive chain.
// 1.03 - fixup smartort bus enable and reset handling, will now start ok with llc reset
// 1.04 - fixup rddata line handling so that it will work with internal 5.25 drive. Now if
//        there is a disk in the floppy drive, it will boot first. Otherwise it will boot
//        from the CF
// 1.05 - fix problem with internal disk being always write protected. Code was stuck in
//        receivepacket loop, so wrprot(ACK) was always 1. Added timeout support for
//        receivepacket function, so this now times out and goes back to the main loop for
//        another try.
// 1.1  - added support for being connected after unidisk 3.5. Need some more io pins to
//        support pass through, this is the next step.
//
// 1.12 - add eeprom storing of boot partition, cycling by eject button (PA3)
//
// 1.13 - Fixed an issue where the block number was calculated incorrectly, leading to
//        failed operations. Now Total Replay v3 works!
//
// 1.15 - Now uses FAT filesystem on the SD card instead of raw layout.
//
//
// 1.16 - Add support .hdv and .2mg (also .po), if add config.txt to the root of the sd card,
//        it will read any file name. images added to subfolders will also be loaded.
//        if not specified in config.txt, usage is the same as in versions up to 1.15.
//        improvement by Wing Yueng.
//
// 1.17 - Add support IIcPlus. Fixed memory allocation to dynamic allocation.
// 
// 1.17b - The version was branched off by TT Design for Micro Smartport board design.
//         Updated UX for changing the boot disk which also allows for soft reboot.
//         Configured serial print statements as a compile option.
//         Changed LED writes to use direct port manipulation.
//         Changed eject button read to use direct port reads.
//         Depricated print_hd_info() since it mixes essential startup code with print statements.
//         Removed some old commented code for readability; more cleanup is needed.
//
// Apple disk interface is connected as follows:
// wrprot = pa5 (ack) (output)
// ph0    = pd2 (req) (input)
// ph1    = pd3       (input)
// ph2    = pd4       (input)
// ph3    = pd5       (input)
// rddata = pd6       (output from avr)
// wrdata = pd7       (input to avr)
//
//led i/o = pa4  (for led on when i/o on boxed version)
//eject button = pa3  (for boxed version, cycle between boot partitions)
//
//
// Serial port was connected for debug purposes. Most of the prints have been commented out.
// I left these in and these can be uncommented as required for debugging. Sometimes the
// prints add to much delay, so you need to be carefull when adding these in.
//
// NOTE: This is uses the ata code from the fat/fat32/ata drivers written by
//       Angelo Bannack and Giordano Bruno Wolaniuk and ported to mega32 by Murray Horn.
//
//*****************************************************************************

#include "SdFat.h"
#include <avr/eeprom.h>

#include <avr/io.h>
#include <string.h>
#include <stdio.h>
#include <avr/pgmspace.h>

#define DEBUG 0

#define PORT_REQ PORTD  // Define the PORT to REQ
#define PIN_REQ 2       // Define the PIN number to REQ
#define PORT_ACK PORTC  // Define the PORT to ACK
#define PIN_ACK 5       // Define the PIN number to ACK

#define NUM_PARTITIONS 4  // Number of 32MB Prodos partions supported


void print_hd_info(void);
void encode_data_packet(unsigned char source);  //encode smartport 512 byte data packet
int decode_data_packet(void);                   //decode smartport 512 byte data packet
void encode_write_status_packet(unsigned char source, unsigned char status);
void encode_init_reply_packet(unsigned char source, unsigned char status);
void encode_status_reply_packet(struct device d);
int packet_length(void);
int partition;
bool is_valid_image(File imageFile);

extern "C" unsigned char ReceivePacket(unsigned char *);  //Receive smartport packet assembler function
extern "C" unsigned char SendPacket(unsigned char *);     //send smartport packet assembler function

unsigned char *packet_buffer;  //Wing

unsigned char status, packet_byte;
int count;
int initPartition;

// We need to remember several things about a device, not just its ID
struct device {
  File sdf;
  unsigned char device_id;     //to hold assigned device id's for the partitions
  unsigned long blocks;        //how many 512-byte blocks this image has
  unsigned int header_offset;  //Some image files have headers, skip this many bytes to avoid them
  bool writeable;
};

device devices[NUM_PARTITIONS];

enum uiState {
  smartport,
  gotch,
  startup
};

uiState state = startup;


//The circuit:
//    SD card attached to SPI bus as follows:
// ** MOSI - pin 11 on Arduino Uno/Duemilanove/Diecimila
// ** MISO - pin 12 on Arduino Uno/Duemilanove/Diecimila
// ** CLK - pin 13 on Arduino Uno/Duemilanove/Diecimila
// ** CS - depends on your SD card shield or module.
//     Pin 10 used here

// Change the value of chipSelect if your hardware does
// not use the default value, SS.  Common values are:
// Arduino Ethernet shield: pin 4
// Sparkfun SD shield: pin 8
// Adafruit SD shields and modules: pin 10

const uint8_t chipSelect = 10;
const uint8_t ejectPin = 17;
const uint8_t statusledPin = 18;

// Don't actually use this, deprecated for simplicity
// Set USE_SDIO to zero for SPI card access.
//
// Initialize at highest supported speed not over 50 MHz.
// Reduce max speed if errors occur.

/*
   Set DISABLE_CHIP_SELECT to disable a second SPI device.
   For example, with the Ethernet shield, set DISABLE_CHIP_SELECT
   to 10 to disable the Ethernet controller.
*/
const int8_t DISABLE_CHIP_SELECT = 0;  // -1
//
// Pin numbers in templates must be constants.

SdFat sdcard;
// Name the SD object different from the above "sd"
// so that if we acciedntally use "sd" anywhere the
// compiler will catch it
//SdBaseFile sdf;
//todo: dynamic(?) array of files selected by user
//File partition1;

//File sdf[4];

// Definitions for direct pin manipulation

// Pin definitions for LED
#define STATUS_LED_PIN 18  // PC4
#define STATUS_LED_PORT PORTC
#define STATUS_LED_DDR DDRC
#define STATUS_LED_BIT 4  // Bit position within port C

// Pin definitions for eject button
#define EJECT_PIN_BIT 3  // PC3
#define EJECT_PIN_PORT PORTC
#define EJECT_PIN_DDR DDRC
#define EJECT_PIN_IN PINC  // Input register for reading

// Helper macros for LED control (only affect PC4)
#define STATUS_LED_ON() STATUS_LED_PORT |= (1 << STATUS_LED_BIT)
#define STATUS_LED_OFF() STATUS_LED_PORT &= ~(1 << STATUS_LED_BIT)

// Helper macro for eject pin reading (only reads PC3)
#define READ_EJECT_PIN() (PINC & (1 << EJECT_PIN_BIT))

//------------------------------------------------------------------------------

void setup() {
  mcuInit();
#if DEBUG
  Serial.begin(230400);
  Serial.print(F("\r\nSmartportSD v1.17a\r\n"));
#endif

  initPartition = eeprom_read_byte(0);
  if (initPartition == 0xFF) initPartition = 0;
  initPartition = (initPartition % 4);

#if DEBUG
  Serial.print(F("\r\nBoot partition: "));
  Serial.print(initPartition, DEC);
#endif

  pinMode(ejectPin, INPUT);
  EJECT_PIN_DDR &= ~(1 << EJECT_PIN_BIT);  // Clear bit 3 to set PC3 as input

  // Init SD card
  if (!sdcard.begin(chipSelect, SPI_HALF_SPEED)) {
#if DEBUG
    Serial.print(F("\r\nError init card"));
#endif
    led_err();
  } else {
    STATUS_LED_ON();
    delay(100);
    STATUS_LED_OFF();
    delay(100);
  }

#if DEBUG
  //print_hd_info();  // depricated because it doesn't just print info
  Serial.print(F("\r\nFree memory before opening images: "));
  Serial.print(freeMemory());
#endif

  String part = "PART";
  ////////<Wing
  SdFile myFile("config.txt", O_READ);
  // check for open error
  if (myFile.isOpen()) {
    unsigned char n;
    packet_buffer = (unsigned char *)malloc(100);
    for (unsigned char i = 0; i < NUM_PARTITIONS; i++) {
      n = myFile.fgets(packet_buffer, 100);
      if (n > 0) {
        if (packet_buffer[n - 1] == '\n') {
          packet_buffer[n - 1] = 0;
        }
#if DEBUG
        Serial.print("\r\n");
        Serial.print((char *)packet_buffer);
#endif
      }
      open_image(devices[i], (char *)packet_buffer);
      if (!devices[i].sdf.isOpen()) {
#if DEBUG
        Serial.print(F("\r\nImage "));
        Serial.print(F(" open error!"));
#endif
      } else {
        if ((packet_buffer[n - 4] == '2') && ((packet_buffer[n - 3] & 0xdf) == 'M') && ((packet_buffer[n - 2] & 0xdf) == 'G')) devices[i].header_offset = 64;
        else devices[i].header_offset = 0;
      }
#if DEBUG
      Serial.print(F("\r\nFree memory after opening image "));
      Serial.print(F(": "));
      Serial.print(freeMemory(), DEC);
#endif
      if (!myFile.available()) break;
    }
    free(packet_buffer);
    myFile.close();
  } else
    ////////Wing>
    for (unsigned char i = 0; i < NUM_PARTITIONS; i++) {
      //TODO: get file names from EEPROM
      open_image(devices[i], (part + (i + 1) + ".po"));
      if (!devices[i].sdf.isOpen()) {
#if DEBUG
        Serial.print(F("\r\nImage "));
        Serial.print(i, DEC);
        Serial.print(F(" open error!"));
#endif
      }
#if DEBUG
      Serial.print(F("\r\nFree memory after opening image "));
      Serial.print(i);
      Serial.print(F(": "));
      Serial.print(freeMemory(), DEC);
#endif
    }
  packet_buffer = (unsigned char *)malloc(605);
}

//*****************************************************************************
// Function: main loop
// Parameters: none
// Returns: 0
//
// Description: Main function for Apple //c Smartport SD adpater
//*****************************************************************************


void loop() {

  // put your main code here, to run repeatedly:

  unsigned long int block_num;
  unsigned char LBH, LBL, LBN, LBT, LBX;

  int number_partitions_initialised = 1;
  int noid = 0;
  int count;
  int ui_command;
  bool sdstato;
  unsigned char source, status, phases, status_code;

  DDRD = 0x00;

  PORTD &= ~(_BV(6));  // set RD low
  interrupts();
  while (1) {
    state = smartport;

    if (READ_EJECT_PIN()) rotate_boot();

    noid = 0;     //reset noid flag
    DDRC = 0xDF;  //set ack (wrprot) to input to avoid clashing with other devices when sp bus is not enabled

    // read phase lines to check for smartport reset or enable
    phases = (PIND & 0x3c) >> 2;

    switch (phases) {

        // phase lines for smartport bus reset
        // ph3=0 ph2=1 ph1=0 ph0=1

      case 0x05:
#if DEBUG
        Serial.print(F("\r\nReset\r\n"));
#endif
        // monitor phase lines for reset to clear
        while ((PIND & 0x3c) >> 2 == 0x05)
          ;
        number_partitions_initialised = 1;                            //reset number of partitions init'd
        noid = 0;                                                     // to check if needed
        for (partition = 0; partition < NUM_PARTITIONS; partition++)  //clear device_id table
          devices[partition].device_id = 0;
        break;

      // phase lines for smartport bus enable
      // ph3=1 ph2=x ph1=1 ph0=x
      case 0x0a:
      case 0x0b:
      case 0x0e:
      case 0x0f:
        noInterrupts();
        DDRC = 0xFF;  //set ack to output, sp bus is enabled
        if ((status = ReceivePacket((unsigned char *)packet_buffer))) {
          interrupts();
          break;  //error timeout, break and loop again
        }
        interrupts();

        // lets check if the pkt is for us
        if (packet_buffer[14] != 0x85)  // if its an init pkt, then assume its for us and continue on
        {
          // else check if its our one of our id's
          for (partition = 0; partition < NUM_PARTITIONS; partition++) {
            if (devices[(partition + initPartition) % NUM_PARTITIONS].device_id != packet_buffer[6])  //destination id
              noid++;
          }
          if (noid == NUM_PARTITIONS)  //not one of our id's
          {
            delay(100);
#if DEBUG
            Serial.print(F("\r\nNot our ID!"));
#endif
            DDRC = 0xDF;         //set ack to input, so lets not interfere
            PORTC &= ~(_BV(5));  //set ack low, for next time its an output
            while (PINC & 0x20)
              ;  //wait till low other dev has finished receiving it
            //assume its a cmd packet, cmd code is in byte 14
            //now we need to work out what type of packet and stay out of the way
            switch (packet_buffer[14]) {
              case 0x80:  //is a status cmd
              case 0x83:  //is a format cmd
              case 0x81:  //is a readblock cmd
                while (!(PINC & 0x20))
                  ;  //wait till high
                while (PINC & 0x20)
                  ;  //wait till low
                while (!(PINC & 0x20))
                  ;  //wait till high
                break;
              case 0x82:  //is a writeblock cmd
                while (!(PINC & 0x20))
                  ;  //wait till high
                while (PINC & 0x20)
                  ;  //wait till low
                //Serial.print(F(("w ")) );
                while (!(PINC & 0x20))
                  ;  //wait till high
                while (PINC & 0x20)
                  ;  //wait till low
                while (!(PINC & 0x20))
                  ;  //wait till high
                break;
            }
            break;  //not one of ours
          }
        }

        //else it is ours, we need to handshake the packet
        PORTC &= ~(_BV(5));  //set ack low
        while (PIND & 0x04)
          ;  //wait for req to go low
        //Not safe to assume it's a normal command packet, GSOS may throw
        //us several extended packets here and then crash
        //Refuse an extended packet
        source = packet_buffer[6];
        //Check if its one of ours and an extended packet

        //assume its a cmd packet, cmd code is in byte 14

        if (packet_buffer[14] >= 0xC0) {
          // Serial.print(F("\r\nExtended packet!"));
          // Serial.print(F("\r\nHere's our packet!"));
          // print_packet ((unsigned char*) packet_buffer, packet_length());
          // delay(50);
        }

        switch (packet_buffer[14]) {

          case 0x80:  //is a status cmd
            STATUS_LED_ON();
            source = packet_buffer[6];
            for (partition = 0; partition < NUM_PARTITIONS; partition++) {  //Check if its one of ours
              if (devices[(partition + initPartition) % NUM_PARTITIONS].device_id == source
                  && devices[(partition + initPartition) % NUM_PARTITIONS].sdf.isOpen()) {  //yes it is, and it's online, then reply
                //Added (unsigned short) cast to ensure calculated block is not underflowing.
                status_code = (packet_buffer[19] & 0x7f);  // | (((unsigned short)packet_buffer[16] << 3) & 0x80);

                if (status_code == 0x03) {  // if statcode=3, then status with device info block
#if DEBUG
                  Serial.print(F("\r\n******** Sending DIB! ********"));
#endif
                  encode_status_dib_reply_packet(devices[(partition + initPartition) % NUM_PARTITIONS]);
                  delay(50);
                } else {  // else just return device status
                  encode_status_reply_packet(devices[(partition + initPartition) % NUM_PARTITIONS]);
                }
                noInterrupts();
                DDRD = 0x40;  //set rd as output
                status = SendPacket((unsigned char *)packet_buffer);
                DDRD = 0x00;  //set rd back to input so back to tristate
                interrupts();

                STATUS_LED_OFF();
              }
            }
            break;
          case 0xC2:
#if DEBUG
            Serial.print(F("\r\nExtended write! Not implemented!"));
#endif
            break;
          case 0xC3:
#if DEBUG
            Serial.print(F("\r\nExtended format! Not implemented!"));
#endif
            break;
          case 0xC5:
#if DEBUG
            Serial.print(F("\r\nExtended init! Not implemented!"));
#endif
            break;

          case 0xC0:  //Extended status cmd
            STATUS_LED_ON();
            source = packet_buffer[6];
            for (partition = 0; partition < NUM_PARTITIONS; partition++) {                      //Check if its one of ours
              if (devices[(partition + initPartition) % NUM_PARTITIONS].device_id == source) {  //yes it is, then reply
                //Added (unsigned short) cast to ensure calculated block is not underflowing.
                status_code = (packet_buffer[21] & 0x7f);
#if DEBUG
                Serial.print(F("\r\nExtended Status CMD:"));
                Serial.print(status_code, HEX);
                print_packet((unsigned char *)packet_buffer, packet_length());
#endif
                if (status_code == 0x03) {  // if statcode=3, then status with device info block
#if DEBUG
                  Serial.println(F("Extended status DIB!"));
#endif
                } else {  // else just return device status
                  encode_extended_status_reply_packet(devices[(partition + initPartition) % NUM_PARTITIONS]);
                }
                noInterrupts();
                DDRD = 0x40;  //set rd as output
                status = SendPacket((unsigned char *)packet_buffer);
                DDRD = 0x00;  //set rd back to input so back to tristate
                interrupts();
                STATUS_LED_OFF();
              }
            }
            break;
          case 0xC1:  //extended readblock cmd
            /*Serial.print(F("\r\nExtended read!"));
            source = packet_buffer[6];
            //Serial.print("\r\nDrive ");
            //Serial.print(source,HEX);
            LBH = packet_buffer[16]; //high order bits
            LBX = packet_buffer[21]; //block number SUPER high! whee
            LBT = packet_buffer[20]; //block number high
            LBL = packet_buffer[19]; //block number middle
            LBN = packet_buffer[18]; //block number low
            for (partition = 0; partition < NUM_PARTITIONS; partition++) { //Check if its one of ours
              if (devices[(partition + initPartition) % NUM_PARTITIONS].device_id == source) {  //yes it is, then do the read
                // block num 1st byte
                //Added (unsigned short) cast to ensure calculated block is not underflowing.
                block_num = (LBN & 0x7f) | (((unsigned short)LBH << 3) & 0x80);
                // block num second byte
                //print_packet ((unsigned char*) packet_buffer,packet_length());
                //Added (unsigned short) cast to ensure calculated block is not underflowing.
                block_num = block_num + (((LBL & 0x7f) | (((unsigned short)LBH << 4) & 0x80)) << 8);
                block_num = block_num + (((LBT & 0x7f) | (((unsigned short)LBH << 5) & 0x80)) << 16);
                block_num = block_num + (((LBX & 0x7f) | (((unsigned short)LBH << 6) & 0x80)) << 24);
                Serial.print(F("\r\n Extended read block #0x"));
                Serial.print(block_num, HEX);
                // partition number indicates which 32mb block we access on the CF
                // block_num = block_num + (((partition + initPartition) % 4) * 65536);

                digitalWrite(statusledPin, HIGH);
                Serial.print(F("\r\nID: "));
                Serial.print(source);
                Serial.print(F("Read Block: "));
                Serial.print(block_num);

                if (!devices[(partition + initPartition) % NUM_PARTITIONS].sdf.seekSet(block_num*512)){
                  Serial.print(F("\r\nRead err!"));
                }
                
                sdstato = devices[(partition + initPartition) % NUM_PARTITIONS].sdf.read((unsigned char*) packet_buffer, 512);    //Reading block from SD Card
                if (!sdstato) {
                  Serial.print(F("\r\nRead err!"));
                }
                encode_extended_data_packet(source);
                //Serial.print(F("\r\nPrepared data packet before Sending\r\n") );
                noInterrupts();
                DDRD = 0x40; //set rd as output
                status = SendPacket( (unsigned char*) packet_buffer);
                DDRD = 0x00; //set rd back to input so back to tristate
                interrupts();
                //if (status == 1)Serial.print(F("\r\nSent err."));
                digitalWrite(statusledPin, LOW);

                //Serial.print(status);
                //print_packet ((unsigned char*) packet_buffer,packet_length());
                //print_packet ((unsigned char*) sector_buffer,15);
              }
            }
            break;
            */

          case 0x81:  //is a readblock cmd

            source = packet_buffer[6];
            LBH = packet_buffer[16];                                                            //high order bits
            LBT = packet_buffer[21];                                                            //block number high
            LBL = packet_buffer[20];                                                            //block number middle
            LBN = packet_buffer[19];                                                            //block number low
            for (partition = 0; partition < NUM_PARTITIONS; partition++) {                      //Check if its one of ours
              if (devices[(partition + initPartition) % NUM_PARTITIONS].device_id == source) {  //yes it is, then do the read
                // block num 1st byte
                //Added (unsigned short) cast to ensure calculated block is not underflowing.
                block_num = (LBN & 0x7f) | (((unsigned short)LBH << 3) & 0x80);
                // block num second byte
                //Added (unsigned short) cast to ensure calculated block is not underflowing.
                block_num = block_num + (((LBL & 0x7f) | (((unsigned short)LBH << 4) & 0x80)) << 8);
                block_num = block_num + (((LBT & 0x7f) | (((unsigned short)LBH << 5) & 0x80)) << 16);

                // partition number indicates which 32mb block we access on the CF
                // block_num = block_num + (((partition + initPartition) % 4) * 65536);

                STATUS_LED_ON();
                ////////<Wing>

                bool seekSuccess = devices[(partition + initPartition) % NUM_PARTITIONS].sdf.seekSet(block_num * 512 + devices[(partition + initPartition) % NUM_PARTITIONS].header_offset);
#if DEBUG
                if (!seekSuccess) {
                  Serial.print(F("\r\nRead seek err!"));
                  Serial.print(F("\r\nPartition #"));
                  Serial.print((partition + initPartition) % NUM_PARTITIONS);
                  Serial.print(F(" block #"));
                  Serial.print(block_num);
                  if (devices[(partition + initPartition) % NUM_PARTITIONS].sdf.isOpen()) {
                    Serial.print(F("\r\nPartition file is open!"));
                  } else {
                    Serial.print(F("\r\nPartition file is closed!"));
                  }
                }
#endif
                sdstato = devices[(partition + initPartition) % NUM_PARTITIONS].sdf.read((unsigned char *)packet_buffer, 512);  //Reading block from SD Card
#if DEBUG
                if (!sdstato) {
                  Serial.print(F("\r\nRead err!"));
                }
#endif
                encode_data_packet(source);
                noInterrupts();
                DDRD = 0x40;  //set rd as output
                status = SendPacket((unsigned char *)packet_buffer);
                DDRD = 0x00;  //set rd back to input so back to tristate
                interrupts();
                STATUS_LED_OFF();
              }
            }
            break;

          case 0x82:  //is a writeblock cmd
            source = packet_buffer[6];
            for (partition = 0; partition < NUM_PARTITIONS; partition++) {                      //Check if its one of ours
              if (devices[(partition + initPartition) % NUM_PARTITIONS].device_id == source) {  //yes it is, then do the write
                // block num 1st byte
                //Added (unsigned short) cast to ensure calculated block is not underflowing.
                block_num = (packet_buffer[19] & 0x7f) | (((unsigned short)packet_buffer[16] << 3) & 0x80);
                // block num second byte
                //Added (unsigned short) cast to ensure calculated block is not underflowing.
                block_num = block_num + (((packet_buffer[20] & 0x7f) | (((unsigned short)packet_buffer[16] << 4) & 0x80)) * 256);
                //get write data packet, keep trying until no timeout
                noInterrupts();
                DDRC = 0xFF;  //set ack to output, sp bus is enabled
                while ((status = ReceivePacket((unsigned char *)packet_buffer)))
                  ;
                interrupts();
                //we need to handshake the packet
                PORTC &= ~(_BV(5));  //set ack low
                while (PIND & 0x04)
                  ;  //wait for req to go low
                // partition number indicates which 32mb block we access on the CF
                // TODO: replace this with a lookup to get file object from partition number
                // block_num = block_num + (((partition + initPartition) % 4) * 65536);
                status = decode_data_packet();
                if (status == 0) {  //ok
                  STATUS_LED_ON();
                  // TODO: add file object lookup
                  ////////<Wing>
                  if (!devices[(partition + initPartition) % NUM_PARTITIONS].sdf.seekSet(block_num * 512 + devices[(partition + initPartition) % NUM_PARTITIONS].header_offset)) {
#if DEBUG
                    Serial.print(F("\r\nWrite seek err!"));
#endif
                  }
                  sdstato = devices[(partition + initPartition) % NUM_PARTITIONS].sdf.write((unsigned char *)packet_buffer, 512);  //Write block to SD Card
                  if (!sdstato) {
#if DEBUG
                    Serial.print(F("\r\nWrite err!"));
#endif
                    status = 6;
                  }
                }
                //now return status code to host
                encode_write_status_packet(source, status);
                noInterrupts();
                DDRD = 0x40;  //set rd as output
                status = SendPacket((unsigned char *)packet_buffer);
                DDRD = 0x00;  //set rd back to input so back to tristate
                interrupts();
              }
              STATUS_LED_OFF();
            }
            break;

          case 0x83:  //is a format cmd
            source = packet_buffer[6];
            for (partition = 0; partition < NUM_PARTITIONS; partition++) {                      //Check if its one of ours
              if (devices[(partition + initPartition) % NUM_PARTITIONS].device_id == source) {  //yes it is, then reply to the format cmd
                encode_init_reply_packet(source, 0x80);                                         //just send back a successful response
                noInterrupts();
                DDRD = 0x40;  //set rd as output
                status = SendPacket((unsigned char *)packet_buffer);
                interrupts();
                DDRD = 0x00;  //set rd back to input so back to tristate
              }
            }
            break;

          case 0x85:  //is an init cmd

            source = packet_buffer[6];

            if (number_partitions_initialised < NUM_PARTITIONS) {                                                //are all init'd yet
              devices[(number_partitions_initialised - 1 + initPartition) % NUM_PARTITIONS].device_id = source;  //remember source id for partition
              number_partitions_initialised++;
              status = 0x80;                                                                                     //no, so status=0
            } else if (number_partitions_initialised == NUM_PARTITIONS) {                                        // the last one
              devices[(number_partitions_initialised - 1 + initPartition) % NUM_PARTITIONS].device_id = source;  //remember source id for partition
              number_partitions_initialised++;
              status = 0xff;  //yes, so status=non zero
            }

            encode_init_reply_packet(source, status);
            //print_packet ((unsigned char*) packet_buffer,packet_length());

            noInterrupts();
            DDRD = 0x40;  //set rd as output
            status = SendPacket((unsigned char *)packet_buffer);
            DDRD = 0x00;  //set rd back to input so back to tristate
            interrupts();
#if DEBUG
            if (number_partitions_initialised - 1 == NUM_PARTITIONS) {
              for (partition = 0; partition < NUM_PARTITIONS; partition++) {
                Serial.print(F("\r\nDrive: "));
                Serial.print(devices[(partition + initPartition) % NUM_PARTITIONS].device_id, HEX);
              }
            }
#endif
            break;
        }
    }
  }
}

//*****************************************************************************
// Function: encode_data_packet
// Parameters: source id
// Returns: none
//
// Description: encode 512 byte data packet for read block command from host
// requires the data to be in the packet buffer, and builds the smartport
// packet IN PLACE in the packet buffer
//*****************************************************************************
void encode_data_packet(unsigned char source) {
  int grpbyte, grpcount;
  unsigned char checksum = 0, grpmsb;
  unsigned char group_buffer[7];

  // Calculate checksum of sector bytes before we destroy them
  for (count = 0; count < 512; count++)  // xor all the data bytes
    checksum = checksum ^ packet_buffer[count];

  // Start assembling the packet at the rear and work
  // your way to the front so we don't overwrite data
  // we haven't encoded yet

  //grps of 7
  for (grpcount = 72; grpcount >= 0; grpcount--)  //73
  {
    memcpy(group_buffer, packet_buffer + 1 + (grpcount * 7), 7);
    // add group msb byte
    grpmsb = 0;
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      grpmsb = grpmsb | ((group_buffer[grpbyte] >> (grpbyte + 1)) & (0x80 >> (grpbyte + 1)));
    packet_buffer[16 + (grpcount * 8)] = grpmsb | 0x80;  // set msb to one

    // now add the group data bytes bits 6-0
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      packet_buffer[17 + (grpcount * 8) + grpbyte] = group_buffer[grpbyte] | 0x80;
  }

  //total number of packet data bytes for 512 data bytes is 584
  //odd byte
  packet_buffer[14] = ((packet_buffer[0] >> 1) & 0x40) | 0x80;
  packet_buffer[15] = packet_buffer[0] | 0x80;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;    //PBEGIN - start byte
  packet_buffer[7] = 0x80;    //DEST - dest id - host
  packet_buffer[8] = source;  //SRC - source id - us
  packet_buffer[9] = 0x82;    //TYPE - 0x82 = data
  packet_buffer[10] = 0x80;   //AUX
  packet_buffer[11] = 0x80;   //STAT
  packet_buffer[12] = 0x81;   //ODDCNT  - 1 odd byte for 512 byte packet
  packet_buffer[13] = 0xC9;   //GRP7CNT - 73 groups of 7 bytes for 512 byte packet




  for (count = 7; count < 14; count++)  // now xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[600] = checksum | 0xaa;       // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[601] = checksum >> 1 | 0xaa;  // 1 c7 1 c5 1 c3 1 c1

  //end bytes
  packet_buffer[602] = 0xc8;  //pkt end
  packet_buffer[603] = 0x00;  //mark the end of the packet_buffer
}

//*****************************************************************************
// Function: encode_data_packet
// Parameters: source id
// Returns: none
//
// Description: encode 512 byte data packet for read block command from host
// requires the data to be in the packet buffer, and builds the smartport
// packet IN PLACE in the packet buffer
//*****************************************************************************
void encode_extended_data_packet(unsigned char source) {
  int grpbyte, grpcount;
  unsigned char checksum = 0, grpmsb;
  unsigned char group_buffer[7];

  // Calculate checksum of sector bytes before we destroy them
  for (count = 0; count < 512; count++)  // xor all the data bytes
    checksum = checksum ^ packet_buffer[count];

  // Start assembling the packet at the rear and work
  // your way to the front so we don't overwrite data
  // we haven't encoded yet

  //grps of 7
  for (grpcount = 72; grpcount >= 0; grpcount--)  //73
  {
    memcpy(group_buffer, packet_buffer + 1 + (grpcount * 7), 7);
    // add group msb byte
    grpmsb = 0;
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      grpmsb = grpmsb | ((group_buffer[grpbyte] >> (grpbyte + 1)) & (0x80 >> (grpbyte + 1)));
    packet_buffer[16 + (grpcount * 8)] = grpmsb | 0x80;  // set msb to one

    // now add the group data bytes bits 6-0
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      packet_buffer[17 + (grpcount * 8) + grpbyte] = group_buffer[grpbyte] | 0x80;
  }

  //total number of packet data bytes for 512 data bytes is 584
  //odd byte
  packet_buffer[14] = ((packet_buffer[0] >> 1) & 0x40) | 0x80;
  packet_buffer[15] = packet_buffer[0] | 0x80;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;    //PBEGIN - start byte
  packet_buffer[7] = 0x80;    //DEST - dest id - host
  packet_buffer[8] = source;  //SRC - source id - us
  packet_buffer[9] = 0xC2;    //TYPE - 0xC2 = extended data
  packet_buffer[10] = 0x80;   //AUX
  packet_buffer[11] = 0x80;   //STAT
  packet_buffer[12] = 0x81;   //ODDCNT  - 1 odd byte for 512 byte packet
  packet_buffer[13] = 0xC9;   //GRP7CNT - 73 groups of 7 bytes for 512 byte packet

  for (count = 7; count < 14; count++)  // now xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[600] = checksum | 0xaa;       // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[601] = checksum >> 1 | 0xaa;  // 1 c7 1 c5 1 c3 1 c1

  //end bytes
  packet_buffer[602] = 0xc8;  //pkt end
  packet_buffer[603] = 0x00;  //mark the end of the packet_buffer
}


//*****************************************************************************
// Function: decode_data_packet
// Parameters: none
// Returns: error code, >0 = error encountered
//
// Description: decode 512 byte data packet for write block command from host
// decodes the data from the packet_buffer IN-PLACE!
//*****************************************************************************
int decode_data_packet(void) {
  int grpbyte, grpcount;
  unsigned char numgrps, numodd;
  unsigned char checksum = 0, bit0to6, bit7, oddbits, evenbits;
  unsigned char group_buffer[8];

  //Handle arbitrary length packets :)
  numodd = packet_buffer[11] & 0x7f;
  numgrps = packet_buffer[12] & 0x7f;

  // First, checksum  packet header, because we're about to destroy it
  for (count = 6; count < 13; count++)  // now xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];

  evenbits = packet_buffer[599] & 0x55;
  oddbits = (packet_buffer[600] & 0x55) << 1;

  //add oddbyte(s), 1 in a 512 data packet
  for (int i = 0; i < numodd; i++) {
    packet_buffer[i] = ((packet_buffer[13] << i + 1) & 0x80) | (packet_buffer[14 + i] & 0x7f);
  }

  // 73 grps of 7 in a 512 byte packet
  for (grpcount = 0; grpcount < numgrps; grpcount++) {
    memcpy(group_buffer, packet_buffer + 15 + (grpcount * 8), 8);
    for (grpbyte = 0; grpbyte < 7; grpbyte++) {
      bit7 = (group_buffer[0] << (grpbyte + 1)) & 0x80;
      bit0to6 = (group_buffer[grpbyte + 1]) & 0x7f;
      packet_buffer[1 + (grpcount * 7) + grpbyte] = bit7 | bit0to6;
    }
  }

  //verify checksum
  for (count = 0; count < 512; count++)  // xor all the data bytes
    checksum = checksum ^ packet_buffer[count];

  if (checksum == (oddbits | evenbits))
    return 0;  //noerror
  else
    return 6;  //smartport bus error code
}

//*****************************************************************************
// Function: encode_write_status_packet
// Parameters: source,status
// Returns: none
//
// Description: this is the reply to the write block data packet. The reply
// indicates the status of the write block cmd.
//*****************************************************************************
void encode_write_status_packet(unsigned char source, unsigned char status) {
  unsigned char checksum = 0;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  int i;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;            //PBEGIN - start byte
  packet_buffer[7] = 0x80;            //DEST - dest id - host
  packet_buffer[8] = source;          //SRC - source id - us
  packet_buffer[9] = 0x81;            //TYPE
  packet_buffer[10] = 0x80;           //AUX
  packet_buffer[11] = status | 0x80;  //STAT
  packet_buffer[12] = 0x80;           //ODDCNT
  packet_buffer[13] = 0x80;           //GRP7CNT

  for (count = 7; count < 14; count++)  // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[14] = checksum | 0xaa;       // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[15] = checksum >> 1 | 0xaa;  // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[16] = 0xc8;  //pkt end
  packet_buffer[17] = 0x00;  //mark the end of the packet_buffer
}

//*****************************************************************************
// Function: encode_init_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the init command packet. A reply indicates
// the original dest id has a device on the bus. If the STAT byte is 0, (0x80)
// then this is not the last device in the chain. This is written to support up
// to 4 partions, i.e. devices, so we need to specify when we are doing the last
// init reply.
//*****************************************************************************
void encode_init_reply_packet(unsigned char source, unsigned char status) {
  unsigned char checksum = 0;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;     //PBEGIN - start byte
  packet_buffer[7] = 0x80;     //DEST - dest id - host
  packet_buffer[8] = source;   //SRC - source id - us
  packet_buffer[9] = 0x80;     //TYPE
  packet_buffer[10] = 0x80;    //AUX
  packet_buffer[11] = status;  //STAT - data status

  packet_buffer[12] = 0x80;  //ODDCNT
  packet_buffer[13] = 0x80;  //GRP7CNT

  for (count = 7; count < 14; count++)  // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[14] = checksum | 0xaa;       // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[15] = checksum >> 1 | 0xaa;  // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[16] = 0xc8;  //PEND
  packet_buffer[17] = 0x00;  //end of packet in buffer
}

//*****************************************************************************
// Function: encode_status_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the status command packet. The reply
// includes following:
// data byte 1 is general info.
// data byte 2-4 number of blocks. 2 is the LSB and 4 the MSB.
// Size determined from image file.
//*****************************************************************************
void encode_status_reply_packet(device d) {

  unsigned char checksum = 0;
  unsigned char data[4];

  //Build the contents of the packet
  //Info byte
  //Bit 7: Block  device
  //Bit 6: Write allowed
  //Bit 5: Read allowed
  //Bit 4: Device online or disk in drive
  //Bit 3: Format allowed
  //Bit 2: Media write protected
  //Bit 1: Currently interrupting (//c only)
  //Bit 0: Currently open (char devices only)
  data[0] = 0b11111000;
  //Disk size
  data[1] = d.blocks & 0xff;
  data[2] = (d.blocks >> 8) & 0xff;
  data[3] = (d.blocks >> 16) & 0xff;


  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;         //PBEGIN - start byte
  packet_buffer[7] = 0x80;         //DEST - dest id - host
  packet_buffer[8] = d.device_id;  //SRC - source id - us
  packet_buffer[9] = 0x81;         //TYPE -status
  packet_buffer[10] = 0x80;        //AUX
  packet_buffer[11] = 0x80;        //STAT - data status
  packet_buffer[12] = 0x84;        //ODDCNT - 4 data bytes
  packet_buffer[13] = 0x80;        //GRP7CNT
  //4 odd bytes
  packet_buffer[14] = 0x80 | ((data[0] >> 1) & 0x40) | ((data[1] >> 2) & 0x20) | ((data[2] >> 3) & 0x10) | ((data[3] >> 4) & 0x08);  //odd msb
  packet_buffer[15] = data[0] | 0x80;                                                                                                //data 1
  packet_buffer[16] = data[1] | 0x80;                                                                                                //data 2
  packet_buffer[17] = data[2] | 0x80;                                                                                                //data 3
  packet_buffer[18] = data[3] | 0x80;                                                                                                //data 4

  for (int i = 0; i < 4; i++) {  //calc the data bytes checksum
    checksum ^= data[i];
  }
  for (count = 7; count < 14; count++)  // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[19] = checksum | 0xaa;       // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[20] = checksum >> 1 | 0xaa;  // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[21] = 0xc8;  //PEND
  packet_buffer[22] = 0x00;  //end of packet in buffer
}


//*****************************************************************************
// Function: encode_long_status_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the extended status command packet. The reply
// includes following:
// data byte 1
// data byte 2-5 number of blocks. 2 is the LSB and 5 the MSB.
// Size determined from image file.
//*****************************************************************************
void encode_extended_status_reply_packet(device d) {
  unsigned char checksum = 0;

  unsigned char data[5];

  //Build the contents of the packet
  //Info byte
  //Bit 7: Block  device
  //Bit 6: Write allowed
  //Bit 5: Read allowed
  //Bit 4: Device online or disk in drive
  //Bit 3: Format allowed
  //Bit 2: Media write protected
  //Bit 1: Currently interrupting (//c only)
  //Bit 0: Currently open (char devices only)
  data[0] = 0b11111000;
  //Disk size
  data[1] = d.blocks & 0xff;
  data[2] = (d.blocks >> 8) & 0xff;
  data[3] = (d.blocks >> 16) & 0xff;
  data[4] = (d.blocks >> 24) & 0xff;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;         //PBEGIN - start byte
  packet_buffer[7] = 0x80;         //DEST - dest id - host
  packet_buffer[8] = d.device_id;  //SRC - source id - us
  packet_buffer[9] = 0xC1;         //TYPE - extended status
  packet_buffer[10] = 0x80;        //AUX
  packet_buffer[11] = 0x80;        //STAT - data status
  packet_buffer[12] = 0x85;        //ODDCNT - 5 data bytes
  packet_buffer[13] = 0x80;        //GRP7CNT
  //5 odd bytes
  packet_buffer[14] = 0x80 | ((data[0] >> 1) & 0x40) | ((data[1] >> 2) & 0x20) | ((data[2] >> 3) & 0x10) | ((data[3] >> 4) & 0x08) | ((data[4] >> 5) & 0x04);  //odd msb
  packet_buffer[15] = data[0] | 0x80;                                                                                                                          //data 1
  packet_buffer[16] = data[1] | 0x80;                                                                                                                          //data 2
  packet_buffer[17] = data[2] | 0x80;                                                                                                                          //data 3
  packet_buffer[18] = data[3] | 0x80;                                                                                                                          //data 4
  packet_buffer[19] = data[4] | 0x80;                                                                                                                          //data 5

  for (int i = 0; i < 5; i++) {  //calc the data bytes checksum
    checksum ^= data[i];
  }
  //calc the data bytes checksum
  for (count = 7; count < 14; count++)  // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[20] = checksum | 0xaa;       // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[21] = checksum >> 1 | 0xaa;  // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[22] = 0xc8;  //PEND
  packet_buffer[23] = 0x00;  //end of packet in buffer
}
void encode_error_reply_packet(unsigned char source) {
  unsigned char checksum = 0;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;    //PBEGIN - start byte
  packet_buffer[7] = 0x80;    //DEST - dest id - host
  packet_buffer[8] = source;  //SRC - source id - us
  packet_buffer[9] = 0x80;    //TYPE -status
  packet_buffer[10] = 0x80;   //AUX
  packet_buffer[11] = 0xA1;   //STAT - data status - error
  packet_buffer[12] = 0x80;   //ODDCNT - 0 data bytes
  packet_buffer[13] = 0x80;   //GRP7CNT

  for (count = 7; count < 14; count++)  // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[14] = checksum | 0xaa;       // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[15] = checksum >> 1 | 0xaa;  // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[16] = 0xc8;  //PEND
  packet_buffer[17] = 0x00;  //end of packet in buffer
}

//*****************************************************************************
// Function: encode_status_dib_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the status command 03 packet. The reply
// includes following:
// data byte 1
// data byte 2-4 number of blocks. 2 is the LSB and 4 the MSB.
// Calculated from actual image file size.
//*****************************************************************************
void encode_status_dib_reply_packet(device d) {
  int grpbyte, grpcount, i;
  int grpnum, oddnum;
  unsigned char checksum = 0, grpmsb;
  unsigned char group_buffer[7];
  unsigned char data[25];
  //data buffer=25: 3 x Grp7 + 4 odds
  grpnum = 3;
  oddnum = 4;

  //* write data buffer first (25 bytes) 3 grp7 + 4 odds
  data[0] = 0xf8;  //general status - f8
  //number of blocks =0x00ffff = 65525 or 32mb
  data[1] = d.blocks & 0xff;          //block size 1
  data[2] = (d.blocks >> 8) & 0xff;   //block size 2
  data[3] = (d.blocks >> 16) & 0xff;  //block size 3
  data[4] = 0x0b;                     //ID string length - 11 chars
  data[5] = 'S';
  data[6] = 'M';
  data[7] = 'A';
  data[8] = 'R';
  data[9] = 'T';
  data[10] = 'P';
  data[11] = 'O';
  data[12] = 'R';
  data[13] = 'T';
  data[14] = 'S';
  data[15] = 'D';
  data[16] = ' ';
  data[17] = ' ';
  data[18] = ' ';
  data[19] = ' ';
  data[20] = ' ';   //ID string (16 chars total)
  data[21] = 0x02;  //Device type    - 0x02  harddisk
  data[22] = 0x0a;  //Device Subtype - 0x0a
  data[23] = 0x01;  //Firmware version 2 bytes
  data[24] = 0x0f;  //

  // Calculate checksum of sector bytes before we destroy them
  for (count = 0; count < 25; count++)  // xor all the data bytes
    checksum = checksum ^ data[count];

  // Start assembling the packet at the rear and work
  // your way to the front so we don't overwrite data
  // we haven't encoded yet

  //grps of 7
  for (grpcount = grpnum - 1; grpcount >= 0; grpcount--)  // 3
  {
    for (i = 0; i < 8; i++) {
      group_buffer[i] = data[i + oddnum + (grpcount * 7)];
    }
    // add group msb byte
    grpmsb = 0;
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      grpmsb = grpmsb | ((group_buffer[grpbyte] >> (grpbyte + 1)) & (0x80 >> (grpbyte + 1)));
    packet_buffer[(14 + oddnum + 1) + (grpcount * 8)] = grpmsb | 0x80;  // set msb to one

    // now add the group data bytes bits 6-0
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      packet_buffer[(14 + oddnum + 2) + (grpcount * 8) + grpbyte] = group_buffer[grpbyte] | 0x80;
  }


  //odd byte
  packet_buffer[14] = 0x80 | ((data[0] >> 1) & 0x40) | ((data[1] >> 2) & 0x20) | ((data[2] >> 3) & 0x10) | ((data[3] >> 4) & 0x08);  //odd msb
  packet_buffer[15] = data[0] | 0x80;
  packet_buffer[16] = data[1] | 0x80;
  packet_buffer[17] = data[2] | 0x80;
  packet_buffer[18] = data[3] | 0x80;
  ;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;
  packet_buffer[6] = 0xc3;         //PBEGIN - start byte
  packet_buffer[7] = 0x80;         //DEST - dest id - host
  packet_buffer[8] = d.device_id;  //SRC - source id - us
  packet_buffer[9] = 0x81;         //TYPE -status
  packet_buffer[10] = 0x80;        //AUX
  packet_buffer[11] = 0x80;        //STAT - data status
  packet_buffer[12] = 0x84;        //ODDCNT - 4 data bytes
  packet_buffer[13] = 0x83;        //GRP7CNT - 3 grps of 7

  for (count = 7; count < 14; count++)  // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[43] = checksum | 0xaa;       // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[44] = checksum >> 1 | 0xaa;  // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[45] = 0xc8;  //PEND
  packet_buffer[46] = 0x00;  //end of packet in buffer
}


//*****************************************************************************
// Function: encode_long_status_dib_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the status command 03 packet. The reply
// includes following:
// data byte 1
// data byte 2-5 number of blocks. 2 is the LSB and 5 the MSB.
// Calculated from actual image file size.
//*****************************************************************************
void encode_extended_status_dib_reply_packet(device d) {
  unsigned char checksum = 0;

  packet_buffer[0] = 0xff;  //sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;         //PBEGIN - start byte
  packet_buffer[7] = 0x80;         //DEST - dest id - host
  packet_buffer[8] = d.device_id;  //SRC - source id - us
  packet_buffer[9] = 0x81;         //TYPE -status
  packet_buffer[10] = 0x80;        //AUX
  packet_buffer[11] = 0x83;        //STAT - data status
  packet_buffer[12] = 0x80;        //ODDCNT - 4 data bytes
  packet_buffer[13] = 0x83;        //GRP7CNT - 3 grps of 7
  packet_buffer[14] = 0xf0;        //grp1 msb
  packet_buffer[15] = 0xf8;        //general status - f8
  //number of blocks =0x00ffff = 65525 or 32mb
  packet_buffer[16] = d.blocks & 0xff;                 //block size 1
  packet_buffer[17] = (d.blocks >> 8) & 0xff;          //block size 2
  packet_buffer[18] = (d.blocks >> 16) & 0xff | 0x80;  //block size 3 - why is the high bit set?
  packet_buffer[19] = (d.blocks >> 24) & 0xff | 0x80;  //block size 3 - why is the high bit set?
  packet_buffer[20] = 0x8d;                            //ID string length - 13 chars
  packet_buffer[21] = 'Sm';                            //ID string (16 chars total)
  packet_buffer[23] = 0x80;                            //grp2 msb
  packet_buffer[24] = 'artport';
  packet_buffer[31] = 0x80;  //grp3 msb
  packet_buffer[32] = ' SD    ';
  packet_buffer[39] = 0x80;  //odd msb
  packet_buffer[40] = 0x02;  //Device type    - 0x02  harddisk
  packet_buffer[41] = 0x00;  //Device Subtype - 0x20
  packet_buffer[42] = 0x01;  //Firmware version 2 bytes
  packet_buffer[43] = 0x0f;
  packet_buffer[44] = 0x90;  //

  for (count = 7; count < 45; count++)  // xor the packet bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[45] = checksum | 0xaa;       // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[46] = checksum >> 1 | 0xaa;  // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[47] = 0xc8;  //PEND
  packet_buffer[48] = 0x00;  //end of packet in buffer
}

//*****************************************************************************
// Function: verify_cmdpkt_checksum
// Parameters: none
// Returns: 0 = ok, 1 = error
//
// Description: verify the checksum for command packets
//
// &&&&&&&&not used at the moment, no error checking for checksum for cmd packet
//*****************************************************************************
int verify_cmdpkt_checksum(void) {
  int count = 0, length;
  unsigned char evenbits, oddbits, bit7, bit0to6, grpbyte;
  unsigned char calc_checksum = 0;  //initial value is 0
  unsigned char pkt_checksum;

  length = packet_length();

  //2 oddbytes in cmd packet
  calc_checksum ^= ((packet_buffer[13] << 1) & 0x80) | (packet_buffer[14] & 0x7f);
  calc_checksum ^= ((packet_buffer[13] << 2) & 0x80) | (packet_buffer[15] & 0x7f);

  // 1 group of 7 in a cmd packet
  for (grpbyte = 0; grpbyte < 7; grpbyte++) {
    bit7 = (packet_buffer[16] << (grpbyte + 1)) & 0x80;
    bit0to6 = (packet_buffer[17 + grpbyte]) & 0x7f;
    calc_checksum ^= bit7 | bit0to6;
  }

  // calculate checksum for overhead bytes
  for (count = 6; count < 13; count++)  // start from first id byte
    calc_checksum ^= packet_buffer[count];

  oddbits = (packet_buffer[length - 2] << 1) | 0x01;
  evenbits = packet_buffer[length - 3];
  pkt_checksum = oddbits | evenbits;

  if (pkt_checksum == calc_checksum)
    return 1;
  else
    return 0;
}

//*****************************************************************************
// Function: print_packet
// Parameters: pointer to data, number of bytes to be printed
// Returns: none
//
// Description: prints packet data for debug purposes to the serial port
//*****************************************************************************
void print_packet(unsigned char *data, int bytes) {
  int count, row;
  char tbs[8];
  char xx;

  Serial.print(F("\r\n"));
  for (count = 0; count < bytes; count = count + 16) {
    sprintf(tbs, ("%04X: "), count);
    Serial.print(tbs);
    for (row = 0; row < 16; row++) {
      if (count + row >= bytes)
        Serial.print(F("   "));
      else {
        Serial.print(data[count + row], HEX);
        Serial.print(" ");
      }
    }
    Serial.print(F("-"));
    for (row = 0; row < 16; row++) {
      if ((data[count + row] > 31) && (count + row < bytes) && (data[count + row] < 129)) {
        xx = data[count + row];
        Serial.print(xx);
      } else
        Serial.print(F("."));
    }
    Serial.print(F("\r\n"));
  }
}

//*****************************************************************************
// Function: packet_length
// Parameters: none
// Returns: length
//
// Description: Calculates the length of the packet in the packet_buffer.
// A zero marks the end of the packet data.
//*****************************************************************************
int packet_length(void) {
  int x = 0;

  while (packet_buffer[x++])
    ;
  return x - 1;  // point to last packet byte = C8
}

//*****************************************************************************
// Function: print_hd_info
// Parameters: none
// Returns: none
//
// Description: print informations about the ATA dispositive and the FAT File System
//*****************************************************************************
void print_hd_info(void) {
  int i = 0;

  // use uppercase in hex and use 0X base prefix
  if (!sdcard.begin(chipSelect, SPI_HALF_SPEED)) {
    Serial.print(F("\r\nError init card"));
    led_err();
  } else {
    STATUS_LED_ON();
    delay(100);
    STATUS_LED_OFF();
    delay(100);
  }
}

void (*resetFunc)(void) = 0;  // create a standard reset function


// ****************************
// rotate_boot helper functions
// ****************************

// Get current boot partition from EEPROM
uint8_t get_boot_partition() {
  return eeprom_read_byte(0) % NUM_PARTITIONS;
}

// Find next valid partition starting from current position
uint8_t find_next_valid_partition(uint8_t current) {
  for (int i = 0; i < NUM_PARTITIONS; i++) {
    uint8_t next = (current + i) % NUM_PARTITIONS;
    if (devices[next].sdf.isOpen()) {
      return next;
    }
  }

#if DEBUG
  Serial.print(F("\r\nNo valid partitions found. Check your SD card and partition files."));
#endif

  return 0;  // Return to first partition if none are valid
}

// Save boot partition to EEPROM and indicate with LED
void save_boot_partition(uint8_t partition) {
  eeprom_write_byte(0, partition);

  // Flash LED pattern to show boot partition selection is final
  while (1) {
    for (int i = 0; i < (partition + 1); i++) {
      STATUS_LED_ON();
      delay(200);
      STATUS_LED_OFF();
      delay(100);
    }
    delay(600);
    close_images();
    resetFunc();
  }
}

//******************************************************************************
// Function: rotate_boot
// Parameters: none
// Returns: none
//
// Description: Cycle by the 4 partition for selecting boot ones, choosing next
// and save it to EEPROM.  Needs soft REBOOT to get new partition. Device resets
// after selection is made.
//******************************************************************************
int rotate_boot(void) {
  const unsigned long TIMEOUT_MS = 2000;  // overall timeout
  const unsigned long DEBOUNCE_MS = 50;   // 50ms debounce
  unsigned long lastButtonPress = millis();
  unsigned long lastDebounceTime = 0;
  int buttonState;
  int lastButtonState = LOW;

  // Start with current partition and find next valid one
  uint8_t currentPartition = get_boot_partition();
  currentPartition = find_next_valid_partition((currentPartition) % NUM_PARTITIONS);

#if DEBUG
  Serial.print(F("\r\nInitial partition selection: "));
  Serial.print(currentPartition, DEC);
#endif

  // Sub-loop waiting for more button presses or timeout
  while ((millis() - lastButtonPress) < TIMEOUT_MS) {
    // Read button with debounce
    int reading = digitalRead(ejectPin);

    if (reading != lastButtonState) {
      lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
      if (reading != buttonState) {
        buttonState = reading;

        if (buttonState == HIGH) {  // Button press detected
          lastButtonPress = millis();
          currentPartition = find_next_valid_partition((currentPartition + 1) % NUM_PARTITIONS);

#if DEBUG
          Serial.print(F("\r\nAdvancing to partition: "));
          Serial.print(currentPartition, DEC);
#endif

          // Flash LED to acknowledge button press
          STATUS_LED_ON();
          delay(100);
          STATUS_LED_OFF();
        }
      }
    }

    lastButtonState = reading;
  }

  // Timeout occurred, save selection and reset
  save_boot_partition(currentPartition);
  return 0;  // Never reached due to reset in save_boot_partition
}

//*****************************************************************************
// Function: led_err
// Parameters: none
// Returns: nonthing
//
// Description: Flashes status led for show error status
//
//*****************************************************************************

void led_err(void) {
  int i = 0;
  interrupts();
#if DEBUG
  Serial.print(F("\r\nError!"));
#endif
  pinMode(statusledPin, OUTPUT);

  for (i = 0; i < 5; i++) {
    STATUS_LED_ON();
    delay(1500);
    STATUS_LED_OFF();
    delay(100);
    STATUS_LED_ON();
    delay(1500);
    STATUS_LED_ON();
  }
}


//*****************************************************************************
// Function: mcuInit
// Parameters: none
// Returns: none
//
// Description: Initialize the ATMega32
//*****************************************************************************
void mcuInit(void) {
  // Input/Output Ports initialization
  PORTC = 0xFF;  // Port A initialization
  DDRC = 0xFF;

  PORTB = 0x00;  // Port B initialization
  //  DDRXB=0x00;

  //  PORTXC=0x00;// Port C initialization
  //  DDRXC=0xFF;

  PORTD = 0xc0;  // Port D initialization
  DDRD = 0x00;   // leave rd as input, pd6

  // Timer/Counter 0 initialization
  // Clock source: System Clock
  // Clock value: Timer 0 Stopped
  // Mode: Normal top=FFh
  // OC0 output: Disconnected
  //ASSR=0x00;
  //TCCR0=0x00;
  //TCNT0=0x00;
  //OCR0=0x00;

  // Timer/Counter 1 initialization
  // Clock source: System Clock
  // Clock value: Timer 1 Stopped
  // Mode: Normal top=FFFFh
  // OC1A output: Discon.
  // OC1B output: Discon.
  // Noise Canceler: Off
  // Input Capture on Falling Edge
  //TCCR1A=0x00;
  //TCCR1B=0x00;
  //TCNT1H=0x00;
  //TCNT1L=0x00;
  //OCR1AH=0x00;
  //OCR1AL=0x00;
  //OCR1BH=0x00;
  //OCR1BL=0x00;

  // Timer/Counter 2 initialization
  // Clock source: System Clock
  // Clock value: Timer 2 Stopped
  // Mode: Normal top=FFh
  // OC2 output: Disconnected
  //TCCR2=0x00;
  //TCNT2=0x00;
  //OCR2=0x00;


  // INT0: Off
  // INT1: Off
  // INT2: Off
  // INT3: Off
  // INT4: Off
  // INT5: Off
  // INT6: Off
  // INT7: Off
  // EICRA=0x00;
  // EICRB=0x00;
  // EIMSK=0x00;
  // GICR = 0;

  // Timer(s)/Counter(s) Interrupt(s) initialization
  // TIMSK=0x00;
  // ETIMSK=0x00;

  // USART initialization
  // Communication Parameters: 8 Data, 1 Stop, No Parity
  // USART Receiver: Off
  // USART Transmitter: On
  // USART Mode: Asynchronous
  // USART Baud rate: 57600 (double speed = 115200)
  // UCSRA=0x02;
  // UCSRB=0x08;
  // UCSRC=0x06;
  // UBRRH=0x00;
  // UBRRL=0x0e;


  // Analog Comparator initialization
  // Analog Comparator: Off
  // Analog Comparator Input Capture by Timer/Counter 1: Off
  // Analog Comparator Output: Off
  ACSR = 0x80;
  //  SFIOR=0x00;
  //noInterrupts();
}

#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char *sbrk(int incr);
#else   // __ARM__
extern char *__brkval;
#endif  // __arm__

int freeMemory() {
  extern int __bss_end;
  //extern int *__brkval;
  int free_memory;
  if ((int)__brkval == 0) {
    // if no heap use from end of bss section
    free_memory = ((int)&free_memory) - ((int)&__bss_end);
  } else {
    // use from top of stack to heap
    free_memory = ((int)&free_memory) - ((int)__brkval);
  }
  return free_memory;
}


// TODO: Allow image files with headers, too
// TODO: Respect read-only bit in header

bool open_image(device &d, String filename) {
  d.sdf = sdcard.open(filename, O_RDWR);
#if DEBUG
  Serial.print(F("\r\nTesting file "));
#endif
  if (!d.sdf.isOpen() || !d.sdf.isFile()) {
#if DEBUG
    Serial.print(F("\r\nFile must exist, be open and be a regular "));
    Serial.print(F("file before checking for valid image type!"));
#endif
    return false;
  }
  if (d.sdf.size() == 0) {
#if DEBUG
    Serial.print(F("\r\nFile must be an unadorned ProDOS order image with no header!"));
    Serial.print(F("\r\nThis means its size must be an exact multiple of 512!"));
#endif
    return false;
  }
#if DEBUG
  Serial.print(F("\r\nFile good!"));
#endif
  d.blocks = d.sdf.size() >> 9;

  return true;
}

// Added to close files for self-reset

bool close_images() {
  bool success = true;

  // Close all device files
  for (unsigned char i = 0; i < NUM_PARTITIONS; i++) {
    if (devices[i].sdf.isOpen()) {
#if DEBUG
      Serial.print(F("\r\nClosing image "));
      Serial.print(i, DEC);
#endif

      // Ensure all data is written before closing
      devices[i].sdf.sync();

      // Close the file
      if (!devices[i].sdf.close()) {
#if DEBUG
        Serial.print(F("\r\nError closing image "));
        Serial.print(i, DEC);
#endif
        success = false;
      } else {
#if DEBUG
        Serial.print(F("\r\nSuccessfully closed image "));
        Serial.print(i, DEC);
        Serial.print(F("\r\nFree memory after closing image: "));
        Serial.print(freeMemory(), DEC);
#endif
      }
    }
  }

  return success;
}

bool is_ours(unsigned char source) {
  for (unsigned char partition = 0; partition < NUM_PARTITIONS; partition++) {        //Check if its one of ours
    if (devices[(partition + initPartition) % NUM_PARTITIONS].device_id == source) {  //yes it is
      return true;
    }
  }
  return false;
}
