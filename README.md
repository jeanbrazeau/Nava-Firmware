# Nava 2024 Oortone firmware
work in progress, beta comming soon...text below is not finished

## About the Nava Oortone firmware for Nava
Nava is a hardware replica of the legendarry Roland TR909 Drummachine. The analog sound circuits are almost identical to the original while the firmware and sequencer is quite different. There are at least two previous takes on this firmware:
* Final version of the original, "official" firmare, called *1.028beta*. [Firmare](http://www.e-licktronic.com/forum/viewtopic.php?t=864), [Source code](https://github.com/e-licktronic/Nava-v1.0).
* The 2021 Neuromancer version [Firmware and source](https://github.com/BenZonneveld/Nava-2021-Firmware/releases/tag/Nava2021Neuro-20211030).

The version found here is called Nava Oortone (0Tone) and draws heavily on the previous versions but with a few improvements and changes. Please follow [discussion thread on E-Lickronic](http://www.e-licktronic.com/forum/viewtopic.php?t=3076) for details.

### Main differences in this version compared to previous
* All patterns in current bank can be programmed independently without the need to save when changing pattern
* Only need to save when changing banks or entering Track Mode
* Better Working pattern chains (groups) that can be programmed on the fly
* Groups can not be saved since it serves no real purpose
* Button logic improvements with less unexpected results
* Working metronome
* Improved External Instruments (midi note sequencer)

## For developers
I am not an expert on embedded systems and have almost completely kept my hands off code related to triggering, timing and hardware related details. I also believe these sections work pretty well. Mainly this take on the firmware tries to improve button logic and how programmed patterns are handled by the memory trying to avoid the drawbacks of slow EEprom reading and writing. I have developed by uploading via sysex which is slow, with no debugging options but easy to do.

### Tools and methods used:
* Arduino IDE version XXX on macOS Mojave
* macOS Python version (when converting to sysex)
* Development setup: I followed the instructions found [here] (https://github.com/sandormatyi/Nava-909-firmware) but have no further knowledge how the sysexconversion works.


This section is for release page WILL MOVE
#Nava alternative firmware 2024 - beta release
2024 Oortone alternative firmware for the Nava DIY 909 drum machine project. Please be aware that this is a beta version. Sysex with this firmware will be released here pretty soon.
Follow [the thread on the E-licktronic forums] (https://www.e-licktronic.com/forum/viewtopic.php?t=3076) to keep up to date.

Upload firmware using midi sysex.
I use an ESI class compliant midi interface and Sysex Librarian 1.5.1 on macOS using these settings and it works without errors every time:
* Pause between messages 12 ms
* Transmit buffer size: default

Erase EE-prom. This is only needed the first time when switchng over to the Nava Oortone version of the firmware. BEWARE! All pattern and track data will be erased.
Upload to the machine
Unzip downloaded file and load the resulting `Nava0tone_0.90b.syx` into your sysex transmitting software
Connect midi out on interface to midi in on Nava
Turn on Nava holding step buttons 1, 3, 5
Start sysex transmission within 5 seconds
Don't turn off during transmission
Machine will restart when done and you should see "Nava 0tone0.90b"on startup screen. If not, it failed.
Turn on Nava holding Start and Stop/Continue buttons
