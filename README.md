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
* Config pages are selectable from the lit step buttons, not only by cycling SHIFT+TEMPO:
  config mode lights one step button per page, and pressing it goes straight there
* Working metronome
* Improved External Instruments (midi note sequencer)
* External Instrument steps have two velocity levels, programmed like the analog voices:
  press a step once for the soft level, again for the loud one, a third time to clear.
  Each of the 16 MIDI tracks keeps its own levels, so one track can accent where another
  does not. The two MIDI velocities are set on config page 3 (step button 3 in config
  mode, or SHIFT+TEMPO three times; encoder button moves between the fields), and
  default to 63 and 111.

## Flashing firmware over MIDI SysEx

The Nava is updated by putting it into bootloader mode and pushing a `.syx` file at it
over MIDI. Nothing on the panel confirms success afterwards, so it is worth getting the
sequence exactly right.

### Entering bootloader mode

1. **Stop the sequencer.** Config mode cannot be entered while it is running, and it
   closes itself if you start playback (`Seq.ino`).
2. Hold **SHIFT** and press **TEMPO**. This opens config page 1.
3. Press the **last step button** (5 on a PlatformIO build, 4 on an Arduino IDE one),
   or keep pressing **SHIFT + TEMPO** to step through the pages, until the display
   reads:

   ```
      BOOTLOADER
   SHIFT+ENC = GO
   ```

4. Keeping **SHIFT** held, press the **encoder button**. SHIFT qualifies the press
   because a bare encoder press moves between fields on every other config page,
   including the one immediately before this - the same motion one page back would
   otherwise leave the firmware.

   The unit saves first: pattern bank, track, setup and the ext note map are all
   committed to EEPROM, and the transport is stopped with note-offs sent, so nothing is
   left sounding on an external synth. The display shows `Saving...`, then
   `Entering / Bootloader Mode` for a second, then jumps to the bootloader at `0x1F000`.
5. Send the `.syx` file. The screen stays as it is; the panel is no longer running the
   firmware, so it will not react until the transfer finishes and the unit restarts.

> **The jump address is unverified.** `0x1F000` is correct only if the unit is fused
> `BOOTSZ=01`; nothing in this repository records the factory fuses, and the released
> `Nava0tone_0.90b.syx` carries its own loader at `0x1FE00`. If the jump misses, the panel
> looks exactly the same as a successful entry - `nava flash` sends pages blind, with no
> handshake or acknowledgement - and the only symptom is that the firmware is unchanged
> afterwards. Recovery is a power cycle. `avrdude -c usbasp -p m1284p -U hfuse:r:-:h`
> reads the fuses and settles both this and whether `BOOTRST` makes a power cycle the
> real entry.

**In config mode the lit step buttons select the page.** Config mode blinks one step
LED per available page, and pressing that step button goes straight there - so the table
below doubles as the step-button map. SHIFT + TEMPO still cycles forward and wraps back
to page 1 past the last one. Which number BOOTLOADER carries depends on how the firmware
was built:

| Build | Pages | BOOTLOADER page | Step button / SHIFT+TEMPO presses |
|---|---|---|---|
| PlatformIO (`platformio.ini` sets `-DMIDI_HAS_SYSEX=1`) | 1, 2, 3 = ext velocity, 4 = SysEx dump, 5 | **5** | 5 |
| Arduino IDE (`features.h` leaves `MIDI_HAS_SYSEX` commented out) | 1, 2, 3 = ext velocity, 4 | **4** | 4 |

### Building and sending

`tools/` provides a `nava` command that does the whole job - see
[tools/README.md](tools/README.md) for the full reference.

```bash
uv tool install "git+https://github.com/jeanbrazeau/Nava-Firmware#subdirectory=tools[tui]"
nava build                                                   # compile, emit the .syx
nava flash .pio/build/nava_sysex/firmware.syx --out NAVA-909
```

The bare URL takes the default branch; add `@BRANCH` to install from an unmerged
one. `pip install -e "tools[tui]"` works too, from a clone.

`nava build` wraps `pio run -e nava_sysex`; the build also writes the `.syx` on its own
as a post-action. With the Arduino IDE, compile there and convert the `.hex`:

```bash
nava hex2syx path_to_hex_file.hex -o output.syx
```

The 250 ms default between pages is deliberate - the bootloader writes a flash page per
message and will drop data if pushed faster.

### Finding the right MIDI port

`nava ports` enumerates them, but do not trust the names alone if your interface has
several similarly labelled ports (a Mio can easily present both `909/MPC` and
`NAVA-909`). Give `--out` a distinctive substring rather than an index, because indices
shift whenever a USB device is added or removed; an ambiguous match is refused rather
than guessed at.

To confirm which port is physically the Nava, listen instead of guessing: press PLAY
with the unit in MASTER sync and watch for MIDI clock, which it emits 24 times a quarter
note. If it is slaved to an external clock it generates none - latch GUIDE and program a
few EXT INST steps, and watch for note-ons instead.

## Backing up patterns over MIDI SysEx

Patterns, tracks and the setup record can be read off the unit and written back, so a
firmware update no longer risks the contents of the EEPROM.

```bash
nava tui                                                     # or drive it interactively
nava backup --out NAVA-909 --in NAVA-909 -o nava-backup.syx
nava restore nava-backup.syx --out NAVA-909 --in NAVA-909
```

`nava tui` browses backups as decoded step grids, picks the MIDI ports, and runs
dumps, restores and firmware flashes behind confirmation prompts. `nava show
backup.syx C3` prints a single pattern without opening the interface.

Stop the sequencer and press **SHIFT + TEMPO** to the SysEx page (`type / select`)
first - that is where the firmware listens. Entering that page flushes pending edits to
EEPROM and leaving it reloads the current bank, so a restore takes effect without a
power cycle. The same page still dumps a single bank, pattern, track or the config from
the panel with ENTER, which is what the encoder selects there.

Backups are plain `.syx` files of dump messages: `nava inspect` describes one, and any
SysEx utility can replay it. Each item is checksummed and acknowledged, and the unit
verifies a record before writing any of it, so a corrupted transfer leaves the stored
pattern intact rather than half-replaced.

This needs a PlatformIO build; the Arduino IDE build leaves `MIDI_HAS_SYSEX` off in
`features.h` and compiles no SysEx support at all.

## For developers
I am not an expert on embedded systems and have almost completely kept my hands off code related to triggering, timing and hardware related details. I also believe these sections work pretty well. Mainly this take on the firmware tries to improve button logic and how programmed patterns are handled by the memory while also trying to avoid the drawbacks of slow EE-prom reading and writing. I have developed by uploading the firmware to Nava via sysex. This is a slow process, with no debugging options but it's easy to get started. I have no intentions of making a big re write at this time and have tried to follow the main design already implemented by others, although sometimes it's pretty strange stuff. :-D

### Tools and methods used:
* Arduino IDE version 2.0.4 on macOS Mojave, Intel
* macOS Python version 2.7.16 (when converting to sysex)
* Development setup: I followed the instructions found [here](https://github.com/sandormatyi/Nava-909-firmware) and have no further knowledge how the conversion from the compiled Arduino code to Midi System Exclusive works. I have had no issues with these things though, it seems to work flawlessly.

If you get strange midi errors it might have to do with IDE-versions or Midi Library versions but unfortunately I don't know exactly when these problems occur but I've seen them.



