# Nava 2024 Oortone firmware
The latest release of Nava 2024 Oortone firmware is available in the Release section in the right column, click **"Releases"**.

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
I am not an expert on embedded systems and have almost completely kept my hands off code related to triggering, timing and hardware related details. I also believe these sections work pretty well. Mainly this take on the firmware tries to improve button logic and how programmed patterns are handled by the memory while also trying to avoid the drawbacks of slow EE-prom reading and writing. I have developed by uploading the firmware to Nava via sysex. This is a slow process, with no debugging options but it's easy to get started. I have no intentions of making a big re write at this time and have tried to follow the main design already implemented by others, although sometimes it's pretty strange stuff. :-D

### Tools and methods used:
* Arduino IDE version 2.0.4 on macOS Mojave, Intel
* macOS Python version 2.7.16 (when converting to sysex)
* Development setup: I followed the instructions found [here](https://github.com/sandormatyi/Nava-909-firmware) and have no further knowledge how the conversion from the compiled Arduino code to Midi System Exclusive works. I have had no issues with these things though, it seems to work flawlessly.

If you get strange midi errors it might have to do with IDE-versions or Midi Library versions but unfortunately I don't know exactly when these problems occur but I've seen them.



