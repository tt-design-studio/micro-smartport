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

## SD Card Configuration

The MicroSD card needs to be formatted in **FAT32** or **FAT16** format (exFAT is not supported). A regular SanDisk 16GB card (plain black version) formatted in FAT32 was used for testing the cards after they were flashed. A high performance card is not necessary.

Files in the formats of ".po", ".hdv", ".2mg" up to 32 MB are supported. Up to four files are supported.

There are two ways to set up the sd card:

1. Use config.txt file
2. Name files as `PART1.po`, `PART2.po`, `PART3.po`, `PART4.po`

Todo: Add support for ".hdv" and ".2mg" to the above second option.

### `config.txt` Text File Format

Place the text file in the SD card root with the file names you want to serve to the comupter.

```
your_random_file.po
your_random_file.hdv
your_random_file.2mg
another_random_file.po
```

### Compatibility Notes on Supported Apple II Computers

The compatibility of this device remains the same as the SP2SD and other similar Smartport drive emulators. Here is a summary of the different caveats.

**Apple IIgs**
Even when the startup slot is set to #5 and all other bootable devices are removed, the startup timing may not always align perfectly. In such cases, use the shortcut **Open Apple + Control + Reset** to reboot. New users should ensure that the **Open Apple** key is released last to facilitate a smoother reboot process.

**Apple IIc and Apple IIc Plus**
Smartport functionality is not automatically enabled on the Apple IIc if the system is equipped with ROM 255 (or possibly ROM0). A ROM upgrade is necessary; please refer to the section below for additional details.

**Apple IIe, IIe Enhanced, and IIe Platinum**
These models perform reliably with the Grappler Minus card combined with SoftSP DIY ROM versions V2, V3, or V6-a. The card should be installed in slot #5 and accessed through the DiskII card in slot #6, effectively linking Smartport functionality between the two cards. Alternatively, similar results can be achieved using the Liron card or BMOW’s advanced "Yellowstone" card.

**Apple II Plus**
This model is also compatible with the Grappler Minus card when used with SoftSP DIY ROM versions V2, V3, or V6-a.

### Notes on Using the Apple IIc Smartport

For the Apple IIc, Smartport will remain disabled if the system is running ROM 255 (or potentially ROM0). To verify your ROM version, execute the following command at the Basic prompt:

```
PRINT PEEK(64447)
```

If the output is **255**, a ROM replacement is required. Smartport is supported with ROM versions labeled **3**, **4**, or **4x**; systems displaying these numbers do not need a replacement.

Updated ROMs are available online (or you can program your own EPROM), including the enhanced and  updated (2018) **ROM4X**. This version is highly recommended for optimal performance.

## Flashing ATMEGA 328p MCU

The board has a standard ICSP header footprint for flashing. I use a right angle male pin header pressed against the board to make temporary contact for programming. The pin header is attached to a 6-pin to 10-pin female header socket adapter. This adapter usually comes with a USBasp AVR programmer which can be found on Amazon, eBay, etc. MCU power and IO is at 5V, SD card powre and IO is at 3.3V.

Install avrdude (use brew on MacOS). For a new chip, set the fuses to enable 16 MHz clock speed.

```
avrdude -c  usbasp -B 3 -p m328p -U lfuse:w:0xFF:m -U hfuse:w:0xDA:m -U efuse:w:0xFD:m
```

Flash command:

```
avrdude -c usbasp -B 0.1 -p m328p -U flash:w:SmartportSD-1.17b.ino.hex:i
```


---


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
