# downtown-solutions_firmware/

The Arduino sources for the ATmega1284p. `.ino` files are concatenated in name
order before compiling, so a rename reorders definitions.

## Files

| File | What | When to read |
| ---- | ---- | ------------ |
| `README.md` | Design record: why the sequencer, EXT_INST editor, SysEx transfer and boot animation work as they do | Before changing any of them; understanding a decision the code does not explain |
| `downtown-solutions_firmware.ino` | `setup()` and `loop()`; MIDI instance and its `MySettings` | Changing boot order, loop ordering, MIDI library settings |
| `Seq.ino` | All six sequencer modes, pattern selection, groups, track programming, EXT_INST edit block | Adding a mode, changing button meaning, pattern/track editing behaviour |
| `SeqConf.ino` | Config pages and `SetConfigPage()` | Adding a config page, changing what ENTER saves |
| `SeqFunc.ino` | Sequencer helpers, `SanitizePattern()` | Adding a pattern operation; changing what is clamped on load |
| `Clock.ino` | Timer1/2/3 ISRs and `CountPPQN()`: step triggering, shuffle, direction, DIN sync, ext note queue | Anything timing-critical; step trigger path; MIDI clock output |
| `timer.ino` | Timer initialisation | Changing prescalers or timer allocation |
| `Button.ino` | `ButtonGet()` and the per-button state machines over 5 shift registers | Adding a button gesture, debouncing, three-state velocity, mute/solo |
| `Enc.ino` | Rotary encoder, including the SHUFFLE and TEMPO qualifiers | Changing what the encoder edits in a given mode |
| `key.ino` | EXT_INST track editor (`ExtInstUpdate()`) and note preview | Ext step entry, track selection, preview lifetime. Name is historical - see README |
| `Led.ino` | LED words for step/instrument/mode LEDs, config-page blink | Changing what the panel shows; adding an LED state |
| `LCD.ino` | All 16x2 screens, plus `LcdBootAnimation()` | Adding a screen, changing the splash, the power-on dissolve |
| `Dio.ino` | SPI shift-register reads and writes | Changing pin mapping or the scan/latch sequence |
| `Mux.ino` | Multiplexer addressing for routing the DAC to a voice | Changing voice routing or accent CV |
| `Midi.ino` | MIDI in/out, note handling, clock sync, the ext note queue and `ServiceExtMidiNotes()` | Ext note transmission, running status, note-off bookkeeping |
| `Expander.ino` | Expander mode: triggers in, MIDI out | Changing expander behaviour |
| `EEprom.ino` | Pattern/track/setup records, paging, `InitEEprom()` | Changing the stored format or offsets. Record sizes are checked against the host tool |
| `define.h` | Every constant, struct and global | Adding state; looking up a constant, a mode enum or a config page name |
| `features.h` | Compile-time switches (`MIDI_HAS_SYSEX`, `MIDI_EXT_CHANNEL`, ...) | Turning a feature on or off; explaining an Arduino-IDE vs PlatformIO difference |
| `function_declarations.h` | Forward declarations across `.ino` files | Adding a function called from another file |
| `version.h` | `FIRMWARE_VERSION`, with a compile-time length assert | Never edit by hand - `scripts/release.py` rewrites it |
| `Sysex.h` | SysEx command set, buffer sizing | Changing the pattern-transfer protocol; must stay in step with nava-tools |
| `sysex_pack.h` | 7-in-8 packing, free of Arduino dependencies so a host test can compile it | Changing packing; `scripts/tests/test_sysex_pack.py` compiles this file |
| `nava_strings.h` | PROGMEM LCD strings and custom characters | Adding panel text; saving RAM on a string |

## Subdirectories

| Directory | What | When to read |
| --------- | ---- | ------------ |
| `src/` | Vendored libraries: `MemoryFree/`, `SPI/`, `WireN/` (custom I2C) | Never edit directly - upstream copies kept for the Arduino IDE build |
