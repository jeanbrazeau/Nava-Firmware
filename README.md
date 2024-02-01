# Nava 2024 Oortone firmware
work in progress, beta comming soon...text below is not finished
The latest release of Nava 2024 Oortone firmware is available in the Release section in the right column. 

## About the Nava Oortone firmware for Nava
Nava is a hardware replica of the legendarry Roland TR909 Drummachine. The analog sound circuits are almost identical to the original while the firmware and sequencer is quite different. There is also a hardware revision called Nava Extra 9 which extend the sonic possibilities beyond the original TR909. This firmware should work with both. 
There are at least two previous takes on this firmware:
* Final version of the original, "official" firmare, called *1.028beta*. [Firmare](http://www.e-licktronic.com/forum/viewtopic.php?t=864), [Source code](https://github.com/e-licktronic/Nava-v1.0).
* The 2021 Neuromancer version [Firmware and source](https://github.com/BenZonneveld/Nava-2021-Firmware/releases/tag/Nava2021Neuro-20211030).

The version found here is called Nava Oortone (0Tone) and draws heavily on the previous versions but with a few improvements and changes. Please follow [discussion thread on E-Lickronic](http://www.e-licktronic.com/forum/viewtopic.php?t=3076) for details.

### Main differences in this version compared to previous
* All patterns in current bank can be programmed independently without the need to save when changing pattern
* Only need to save when changing banks or entering Track Mode
* Better Working pattern chains (groups) that can be programmed on the fly
* Groups can not be saved since it complicates things without any benefits
* Button logic improvements with less unexpected results
* Working metronome
* Improved External Instruments (midi note sequencer)

## For developers
I am not an expert on embedded systems and have almost completely kept my hands off code related to triggering, timing and hardware related details. I also believe these sections work pretty well. Mainly this take on the firmware tries to improve button logic and how programmed patterns are handled by the memory while alsy trying to avoid the drawbacks of slow EEprom reading and writing. I have developed by uploading the firmware to Nava via sysex, This is slow, with no debugging options but it's easy to get started.

### Tools and methods used:
* Arduino IDE version 2.0.4 on macOS Mojave, Intel
* macOS Python version 2.7.16 (when converting to sysex)
* Development setup: I followed the instructions found [here](https://github.com/sandormatyi/Nava-909-firmware) and have no further knowledge how the conversion from the compiled Arduino code to Midi System Exclusive works. I have had no issues with these things though, it seems to work flawlessly.

This section is for release page WILL MOVE
# Nava alternative firmware 2024 - beta release (February 2024)
2024 Oortone alternative firmware for the Nava DIY 909 drum machine project. Please be aware that this is a beta version. Download and install it on your own risk. Sysex with this firmware will be released here pretty soon.
Follow [the thread on the E-licktronic forums](https://www.e-licktronic.com/forum/viewtopic.php?t=3076) to keep up to date, ask questions or report bugs.

## Changes
This first beta release have these main differences compared to the previous firmwares:
* All patterns in current bank can be programmed independently without the need to save when changing pattern
* Only need to save when changing banks or entering Track Mode
* Better Working pattern chains (groups) that can be programmed on the fly
* Groups can not be saved since it complicates things without any benefits
* Button logic improvements with less unexpected results
* Working metronome
* Improved External Instruments (midi note sequencer)

For more details please visit the forum thread at E-licktronic linked above.

## Installation
Upload the firmware using midi sysex.
I use an ESI class compliant midi interface and Sysex Librarian 1.5.1 on macOS using the following settings and it works without errors every time:
* Pause between messages 12 ms
* Transmit buffer size: default

### EEprom init
If this is the first install of the Nava Oortone firmware you should erase the EE-prom or you might get strange error in some patterns. This is only needed the first time when switchng over to the Nava Oortone version. BEWARE! All pattern and track data will be erased.
* Turn on Nava holding Start and Stop/Continue button and follow on screen instructions

###  Installation process
* Unzip the downloaded file and load the resulting `Nava0tone_0.90b.syx` into your sysex transmitting software
* Connect midi out on interface to midi in on Nava
* Turn on Nava holding step buttons 1, 3, 5
* Start sysex transmission within 5 seconds
* Don't turn off during transmission
* Machine will restart when done and you should see `Nava 0tone0.90b` on startup screen. If not, it failed.

