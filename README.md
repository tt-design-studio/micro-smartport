# Micro Smartport Board Summary

![](/images/micro_Smartport_diag.jpeg)


A simple board to serve up to four disk images to a Smartport compatible vintage Apple II type computer. 

The design is based on previous work from the vintage Apple community.

# Product Information

![](/images/micro_smartport_component_side.jpeg)

- [Purchase completed assembly link on Tindie](https://www.tindie.com/products/ttdesign/micro-smartport-microsd-apple-ii-drive-emulator/)


This board is a simple disk drive emulator for Apple II Computers, such as the IIGS, IIc, IIc Plus, and other machines that support Smartport drives in ROM or with an add-on card. 4 disk images can be copied to a MicroSD card and rotated through by holding the eject button on machine turn-on and then rebooting the machine.

## Features:

- Push-push Molex microSD slot
- Side action eject button
- Status LED
- SD card activity LED
- Authentic NOS DB19 connector
- Footprint for soldering ICSP pin header (header not included)
- Compatible with version the open source 1.17 firmware (MIT License)
- Flashed with a firmware with some changes to UX to allow for soft-reboot and switching through more than one image at a time to set the boot disk

Note: Please check/confirm if your machine supports Smartport drives before purchasing. Does not include microSD card.

## Acknowledgements:

- Robert Justice for his original SmartPortCFA project

- Andrea Ottaviani for his Arduino/SD port of Robert's work

- Katherine Stark for adding FAT support: https://gitlab.com/nyankat/smartportsd

- Kay Koba for his version of the board and hosting the latest firmware source: https://github.com/kerokero5150/SP2SD_DIY_KIT

- Rodney Ross, my initial source of inspiration for a minimalist board design: http://rodneyross.com/blog/?page_id=63

## Rather DIY?

### SMD Style Board
Chris Tersteeg has boards for sale in his Tindie store: 
[link](https://www.tindie.com/products/tersteeg/smartportsd-apple-ii-pcb/)

### Through Hole DIY Version
[SPIISD link](https://github.com/kerokero5150/SP2SD_DIY_KIT/tree/main?tab=readme-ov-file)

## Background:

I needed to boot an Apple IIGS to test my ADB optical mouse board for a customer. I threw together some spare parts (arduino, microSD breakout, jumper wires) to make a hacked SmartportSD. It worked so well that I decided (like others) to spin this tiny board. It's compatible with version 1.17 of the firmware.

## Licenses

### Software

Check the firmware section for the software license.

### Hardware

The hardware design is proprietary and copyrighted by TT Design, 2025. All rights reserved.
