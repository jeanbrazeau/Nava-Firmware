# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

The Nava Oortone Firmware is an alternate firmware for the Nava hardware, which is a replica of the Roland TR909 drum machine. This firmware enhances the original Nava with improved pattern chaining, button logic, metronome, and external instrument (MIDI) functionality.

## Hardware Architecture

The Nava uses an ATmega1284p microcontroller running at 16MHz, interfacing with:

1. **Shift Registers** - For reading button states and controlling LEDs and triggers through SPI
2. **DAC (Digital-to-Analog Converter)** - For controlling velocity/accent of drum voices
3. **Multiplexers** - For routing signals to various drum voice circuits
4. **LCD Display** - 16x2 character display for user interface
5. **MIDI Interface** - For synchronization and note input/output
6. **DIN Sync** - For synchronizing with older drum machines

Key hardware systems include:
- **Digital I/O System**: Uses SPI to communicate with shift registers for buttons and LEDs
- **Analog Triggers**: Controls drum voice circuits with velocity values
- **Sequencer**: 16-step pattern sequencer with pattern chaining capability
- **Pattern Storage**: Uses EEPROM for long-term storage with RAM buffers for active patterns

## Repository Structure

- `downtown-solutions_firmware/`: Main firmware directory containing Arduino code
  - `downtown-solutions_firmware.ino`: Main entry point for the Arduino firmware
  - Various `.ino` files for different functional components (Button, Clock, Dio, etc.)
  - `define.h`: Contains all definitions, constants, and global variables
  - `features.h`: Feature toggles and configuration options
  - `src/`: External libraries
    - `MemoryFree/`: Memory management utilities
    - `SPI/`: SPI communication library
    - `WireN/`: I2C communication library (custom)
- `tools/`: the `nava` CLI (Python 3) - see `tools/README.md`
  - `nava/bootloader.py`: firmware `.hex` -> bootloader `.syx` (nibblized pages)
  - `nava/protocol.py`: the pattern/track/setup dump protocol, mirroring `Sysex.h`
  - `nava/midiio.py`: port discovery, retries, ACK handling
  - `nava/cli.py`: `build`, `hex2syx`, `flash`, `backup`, `restore`, `inspect`, `ports`
  - `tests/`: runs without hardware; `fakenava.py` models the device

## Development Commands

### Firmware Compilation and Upload

The firmware is developed using Arduino IDE. To compile and upload:

1. Open the downtown-solutions_firmware.ino file in Arduino IDE 2.0.4 (recommended version)
2. Select the appropriate board settings:
   - Board: ATmega1284
   - Processor: ATmega1284 (16MHz)
   - Programmer: USBasp or AVRISP mkII (depending on your hardware)
3. Compile the firmware using the Arduino IDE

### Converting Firmware to SysEx for Upload

A PlatformIO build emits the `.syx` itself as a post-action (`convert_to_sysex.py`).
For an Arduino IDE build, convert the `.hex`:

```bash
pip install -e tools
nava hex2syx path_to_hex_file.hex -o output.syx
nava flash output.syx --out NAVA-909
```

The tools are Python 3. The original Python 2 scripts are gone; `nava`'s encoder is
pinned to their output by a test that reproduces the released `Nava0tone_0.90b.syx`
byte for byte.

On Apple Silicon the AVR toolchain is x86-only (PlatformIO publishes no arm64
build, and neither does Arduino), so a build needs Rosetta:
`softwareupdate --install-rosetta --agree-to-license`.

### Backing up patterns

`nava backup` / `nava restore` read and write patterns, tracks and the setup record
over SysEx while the unit sits on the SysEx config page. See the EXT/SysEx notes
below and `tools/README.md` for the protocol.

## Key Components

- **Sequencer Core**: Implemented across Seq.ino, SeqConf.ino, and SeqFunc.ino
- **Memory Management**: Pattern storage and management in EEprom.ino
- **I/O Handling**: Button, Led, Dio, and Mux handle all input/output operations
- **MIDI Implementation**: Midi.ino handles all MIDI functionality
- **Clock Management**: Clock.ino handles timing, synchronization, and tempo

## Architecture Notes

### Sequencer Design

The firmware uses a dual-pattern buffer system (pattern[2]) that allows one pattern to be edited while another is playing. This enables seamless pattern changes and more flexible pattern chaining (groups).

Patterns are stored in RAM (patternBank[16]) for quick access, and only saved to EEPROM when changing banks or entering Track Mode, improving performance and reducing EEPROM wear.

### Main Loop Architecture

The firmware follows a real-time polling architecture where each loop cycle:
1. Checks MIDI input and expander mode status
2. Drains any external-instrument MIDI the clock queued (`ServiceExtMidiNotes()`, immediately after `MIDI.read()`)
3. Polls buttons and encoders for user input
4. Updates LEDs, LCD, and sequencer parameters
5. Manages sequencer configuration and the external instrument track editor (`ExtInstUpdate()`)

External-instrument notes are transmitted by the clock when that cannot block, and by
the loop otherwise - see "Transmission timing" under the EXT_INST section.

### Key Improvements in this Firmware

- Better memory management with pattern banks stored in RAM
- Improved pattern chaining (groups) that can be programmed on the fly
- Enhanced button logic with more predictable behavior
- Working metronome for rhythm reference
- Improved MIDI integration with external instruments
- More consistent pattern transitions

## Important Variables and Structures

- `Pattern`: The main data structure that stores all information about a pattern
- `pattern[2]`: Double-buffered pattern storage for seamless pattern changes
- `patternBank[16]`: In-memory storage for the current bank of patterns
- `editedPatterns[16]`: Tracks which patterns have been edited and need saving
- `seq`: Global structure containing sequencer configuration
- `curSeqMode`: Current sequencer mode (TRACK_PLAY, TRACK_WRITE, PTRN_PLAY, etc.)

## Development Tips

- The codebase uses many global variables - be careful when modifying any variable as it may be used across multiple files
- Most timing-critical code is in Clock.ino and timer.ino - modify with caution
- When adding new features, follow the existing pattern of implementing functionality in dedicated .ino files
- The firmware targets the ATmega1284p microcontroller with 16MHz clock
- Binary size is important - memory is limited on the target device

---

# Detailed Codebase Analysis

## File Structure (downtown-solutions_firmware/)

### Main Entry Point
- **downtown-solutions_firmware.ino** (206 lines)
  - Setup: Initializes I/O, LCD, MIDI, bootloader check, pattern/track loading
  - Loop: Expander mode, MIDI read, button/encoder polling, LED/LCD updates, sequencer configuration

### Core Modules

#### Sequencer Logic
- **Seq.ino** (1220 lines) - Main sequencer parameter handling
  - Implements all 6 sequencer modes (PTRN_STEP, PTRN_TAP, PTRN_PLAY, TRACK_PLAY, TRACK_WRITE, MUTE)
  - Pattern selection and editing (step/tap programming)
  - Track programming and playback
  - Pattern group/chain functionality
  - External instrument (EXT_INST) edit mode [SIZZLE FW]
  - Pattern bank loading/saving to EEPROM

- **SeqConf.ino** - Sequencer configuration page handling
- **SeqFunc.ino** - Sequencer utility functions

#### Timing & Clock
- **Clock.ino** (196 lines) - Real-time sequencer timing
  - `ISR(TIMER1_COMPA_vect)`: Main clock ISR, calls CountPPQN()
  - `ISR(TIMER2_COMPA_vect)`: Trigger off timer (2ms pulses)
  - `ISR(TIMER3_COMPA_vect)`: Flam timer (delayed second hits)
  - `CountPPQN()`: Processes each PPQN tick (96 per quarter note)
    - Handles shuffle, direction modes, step triggering
    - Sets velocity via DAC, triggers via shift registers
    - Queues external instrument MIDI for the loop to transmit; sends none itself
    - DIN sync clock output
    - End-of-measure pattern/track progression

- **timer.ino** - Timer initialization functions

#### Input Handling
- **Button.ino** (375 lines)
  - `ButtonGet()`: Scans 5 shift registers via SPI
  - `StepButtonGet()`: Toggle vs momentary modes
  - `InstValueGet()`: Three-state velocity (low/high/off), flam mode support
  - `MuteButtonGet()`: Fast mute handling with shift-solo feature
  - `GateButtonGet()`: Gate mode for expander
  - EXT_INST edit mode toggle detection

- **Enc.ino** - Rotary encoder handling
- **key.ino** - External instrument track editor (`ExtInstUpdate()`) and MIDI note preview.
  The filename is historical: .ino files are concatenated in name order, so renaming it
  would reorder definitions.

#### Output Handling
- **Led.ino** - LED control via shift registers
- **LCD.ino** - 16x2 LCD display updates for all modes
- **Dio.ino** - Digital I/O via SPI shift registers
- **Mux.ino** - Multiplexer control for routing triggers to drum voices

#### Hardware Interface
- **EEprom.ino** - Pattern/track storage and retrieval
- **Midi.ino** - MIDI in/out, note handling, clock sync. Owns the external instrument
  note queue: `ServiceExtMidiNotes()` drains it from the loop, `SendExtTrackNoteOff()`
  silences what is sounding, `InitMidiNoteOff()` also discards anything still queued.
- **Expander.ino** - Expander mode (trigger-to-MIDI conversion)

### Configuration Files
- **define.h** (625 lines) - All constants, structures, global variables
- **features.h** (11 lines) - Feature flags (MIDI_BANK_PATTERN_CHANGE, MIDI_EXT_CHANNEL, etc.)
- **function_declarations.h** - Forward declarations for cross-file function calls
- **Sysex.h** - SysEx bootloader support
- **string.h** - Custom LCD character definitions

## Data Structures Deep Dive

### Pattern Structure (393 bytes each)
```cpp
struct Pattern {
  byte length;              // 0-15 (actual step count - 1)
  byte scale;               // SCALE_16, SCALE_32, SCALE_8t, SCALE_16t (24, 12, 32, 16 PPQN)
  byte dir;                 // FORWARD, BACKWARD, PING_PONG, RANDOM
  byte shuffle;             // 0-7 (shuffle type)
  byte flam;                // 0-7 (flam delay time)
  unsigned int inst[16];    // 16-bit word per instrument (each bit = step on/off)
  unsigned int step[16];    // 16-bit word per step (each bit = instrument on/off)
  byte velocity[16][16];    // Velocity per instrument per step
                           // Bit 7 = flam flag, bits 0-6 = velocity value
  unsigned int extTrack[16];// 16-bit word per external track (each bit = step on/off)
  unsigned int extAccent[16];// Second velocity level, per step per track (bit set = high)
  byte extLength;           // Last step of the ext layer, independent of `length`
  byte groupPos;            // Position in pattern group/chain
  byte groupLength;         // Length of pattern group/chain
  byte totalAcc;            // Total accent track
};
```

### Memory Management Strategy
- **pattern[2]**: Twin buffers - edit one (`!ptrnBuffer`) while playing other (`ptrnBuffer`)
- **patternBank[16]**: Current bank cached in RAM (16 patterns × 393 bytes = 6.3KB)
- **editedPatterns[16]**: Boolean flags to track which patterns need EEPROM save
- **bufferedPattern**: Copy/paste buffer for pattern operations
- **tempPattern**: Temporary buffer for EEPROM read/write operations

### Track Structure
```cpp
struct Track {
  byte patternNbr[1024];   // Pattern sequence (0-127 or END_OF_TRACK)
  unsigned int length;     // Track length (0-999)
};
Track track[2];            // Double buffered
```

### Sequencer Configuration
```cpp
struct SeqConfig {
  boolean ptrnChangeSync;  // SYNC = change on bar, FREE = immediate
  byte sync;               // MASTER, SLAVE, EXPANDER
  boolean syncChanged;
  byte TXchannel;          // MIDI transmit channel
  byte RXchannel;          // MIDI receive channel
  byte EXTchannel;         // External instrument MIDI channel
  SeqMode BootMode;        // Mode to boot into
  boolean SysExMode;       // SysEx bootloader mode
  unsigned int bpm;        // Current tempo
  unsigned int defaultBpm; // EEPROM stored tempo
  byte dir;                // Sequencer direction
  byte configPage;         // Current config page (0-MAX_CONF_PAGE)
  boolean configMode;      // In config mode flag
  boolean setupNeedSaved;  // Config needs EEPROM save
  boolean muteModeHH;      // Hi-hat mute mode (link CH/OH)
  byte extVelLow;          // Ext instrument velocity, single tap (default 63)
  byte extVelHigh;         // Ext instrument velocity, double tap (default 111)
};
```

## Sequencer Modes Explained

### PTRN_STEP (Pattern Step Edit)
- Program patterns while stopped or running
- Select instrument, press step buttons to toggle on/off
- Three velocity states: off → low → high → off
- Flam mode: off → low+flam → high+flam → off
- Hold CLEAR + step = remove step while running
- SHIFT + CLEAR = clear entire pattern

### PTRN_TAP (Pattern Tap Edit)
- Real-time tap recording while sequencer runs
- Tap instrument buttons in time with sequencer
- Automatically records at current step position
- CLEAR + instrument = remove notes from current step
- Uses `bufferedPattern` to avoid double-triggering

### PTRN_PLAY (Pattern Play)
- Playback mode with pattern selection
- Select patterns/banks with step buttons
- Double-press two steps = pattern group/chain
- Pattern changes: immediate or sync to bar end

### TRACK_WRITE (Track Write)
- Program track sequences (songs up to 1024 patterns)
- ENTER = write current pattern to track position
- FWD/BACK = navigate track positions
- SHIFT + FWD = insert pattern
- SHIFT + BACK = delete pattern
- LAST STEP = set end of track marker

### TRACK_PLAY (Track Play)
- Plays programmed track sequence
- Auto-advances through track on each bar end
- Loads patterns from EEPROM on-demand

### MUTE
- Toggle instruments on/off during playback
- SHIFT + step = solo instrument (mute all others)
- ENCODER press = unmute all
- Works in all play modes

## Clock & Timing System

### Timer1 (Main Clock)
- Runs at BPM-derived frequency
- Generates 96 PPQN (pulses per quarter note)
- ISR calls `CountPPQN()` every tick
- Sends MIDI clock (every 4 PPQN = 1 MIDI clock)
- DIN sync clock output

### Timer2 (Trigger Off)
- One-shot timer for 2ms trigger pulses
- Started when triggers fire
- ISR sets triggers low after 2ms
- Prevents stuck triggers

### Timer3 (Flam)
- Delayed trigger for flam effect
- 8 delay times: 20, 24, 28, 32, 36, 40, 44, 48ms
- Fires second hit after main trigger
- Only for BD, SD, LT, MT, HT

### Shuffle Implementation
- Array of timing offsets: `{0}, {0, -1}, {0, -2}, ... {0, -6}`
- Alternates polarity each step (swing feel)
- Applied as PPQN offset: `(ppqn + shuffle[type][polarity]) % scale`

### Direction Modes
- **FORWARD**: `curStep = stepCount`
- **BACKWARD**: `curStep = length - stepCount`
- **PING_PONG**: Reverses at start/end
- **RANDOM**: `curStep = random(0, 16)`

## Hardware Interface Details

### SPI Shift Registers
- 5 registers total:
  - `dinSr[0-1]`: 16 step buttons (16-bit word)
  - `dinSr[2]`: First button byte (PLAY, STOP, INST, SHIFT, etc.)
  - `dinSr[3]`: Second button byte (TRK, PTRN, GUIDE, etc.)
  - `dinSr[4]`: Third button byte (TEMPO, MUTE, BANK, ENTER)
- SPI settings: 2MHz, MSBFIRST, MODE0

### DAC (MCP4822)
- 2-channel 12-bit DAC
- Channel A: Velocity/accent CV
- Channel B: Unused
- Multiplexed to 10 drum voices
- Voltage range: 0-4.096V (maps to 0-127 velocity)

### Multiplexer Routing
```cpp
byte muxInst[10] = {LT, SD, BD, MT, HT, HC, RM, CH, CRASH, RIDE};
// Address bits on PORTA[7:5] select which voice receives DAC output
```

### Trigger Outputs
- 16-bit shift register controls all triggers
- Bits 0-15 map to instruments/functions
- Special handling for CH/OH (bits 1-2) to prevent hi-hat circuit noise

## External Instrument (EXT_INST) - TR-909 style track editor

### Feature Overview
- 16 fixed-pitch MIDI tracks, one chromatic note each, all sharing the 16-step grid
- Polyphonic: any number of tracks may fire on the same step
- One velocity shared by all tracks on a step, taken from the EXT_INST velocity table
- Separate MIDI channel (`seq.EXTchannel`) when `MIDI_EXT_CHANNEL` is enabled, else `seq.TXchannel`

### Note Mapping
`extTrackNote[16]` (define.h) holds the note each track transmits, editable per track
with the encoder while in edit mode. Values are literal wire notes: the ext path uses
`MidiSendExtNoteOn`/`MidiSendExtNoteOff`, which do not apply the +12 that
`MidiSendNoteOn`/`MidiSendNoteOff` add for drum notes, so what the encoder sets, the LCD
prints and the wire carries are one number. `EXT_TRACK_NOTES[16]` is now the PROGMEM
default table (48..63), which reproduces the pitches the old fixed table transmitted.

The map is global rather than per pattern: 16 bytes inside `Pattern` would cost ~320
bytes of RAM across the bank cache and buffers and would change the stored pattern
format. It persists in the unused tail of the setup EEPROM block at `EXT_NOTES_OFFSET`,
written once on edit-mode exit rather than per encoder detent (`extNotesNeedSaved`).
A signature byte precedes the record because neither erased state can be assumed - an
erased EEPROM reads 0xFF but `InitEEprom()` zeroes the device, and 0x00 is a legal MIDI
note, so a range check alone would silently tune every track to note 0.

The LCD renders the note as a name via `LcdPrintNoteName()`, under the MIDI 60 = C4
convention that nearly every DAW and modern synth displays, so the track defaults of
48..63 read C3..D#4. The octave therefore runs -1..9 and `C#-1` is the widest result at
four characters, exactly the width of the value field. Sharps only: picking the flat
spelling of the same pitch would need a key signature the sequencer does not have.
Code comments still cite MIDI numbers, since those are unambiguous.

### Edit Mode Activation
- SHIFT + GUIDE toggles the mode; INST + GUIDE also exits it
- Entering forces `curInst = EXT_INST` and selects track 1; leaving restores the
  instrument selected on entry (`extInstPrevInst`), so the mode behaves as a layer
  nested under the instrument selection. EXT_INST is never restored as a selection since
  it drives no drum circuit, and a saved value of it falls back to BD
- `Seq.ino` must not carry its own SHIFT+GUIDE handler. `ButtonGet()` runs first in the
  loop and toggles the flag, so a second handler guarded on `!extInstEditMode` fires only
  on the exit pass and re-selects EXT_INST straight after the restore
- Only active in PTRN_STEP. The TRK, PTRN, TAP and config-entry handlers clear the flag
  via `ExitExtInstEditMode()`. MUTE is the deliberate exception: it assigns `curSeqMode`
  directly so the mode stays re-enterable, which means the flag survives it. Nothing in
  the edit block runs while `curSeqMode != PTRN_STEP`, and `ExtInstUpdate()` retires a
  sustained preview whose owning context has gone away
- Entry and exit paint a splash for 800ms; the deadline is `extInstSplashUntil` and
  `LcdUpdate()` renders it without blocking the loop

### Note Entry
The step buttons swap roles with the transport (`selectingTrack` in `ExtInstUpdate()`):

- Paused, they are track switches. A bare press selects the track and sustains its note
  until release, so the whole map can be auditioned by ear; INST qualifies a programming
  press instead
- Running, the mapping is reversed - a bare press programs, INST selects - because live
  step entry is the point of holding the transport open, and a sustained audition would
  collide with the note the sequencer is already sounding

Programming cycles the step through three states, the same cycle `InstValueGet()` gives
the drum instruments: off -> `seq.extVelLow` -> `seq.extVelHigh` -> off. The pair is
(`extTrack` bit, `extAccent` bit), and the accent bit is cleared on the way out so a step
re-entered later starts at the low level, as a drum step does. Each press auditions for
50ms at the level it just wrote, so the two are told apart by ear while programming. The
audition is issued before the bit is set, because `ExtPreviewOn()` declines a preview the
running sequencer would collide with and setting the bit first would make every addition
look like a collision.

The step LEDs carry the level the way the drum lane does: accented steps are lit on every
pass, unaccented ones on one pass in four, so the panel reads at a glance.

The encoder sets the selected track's note via `ExtSetTrackNote()`, which auditions the
new pitch and moves an open sustained preview to it. Holding TEMPO yields the encoder
back to BPM, and holding SHUFFLE yields it to the shuffle amount, matching every other
PTRN_STEP page - the edit mode is a layer over the instrument selection, not a takeover
of the panel. SHUFFLE also takes back the step buttons, which set shuffle and flam while
it is held; `ExtInstUpdate()` stands down for it exactly as `Seq.ino`'s step handler does.

The SHUFFLE branch of `EncGet()` is not scoped to the edit mode: holding SHUFFLE left the
encoder on BPM everywhere, so the button that owns shuffle could not adjust it. It writes
through the same global `prevShuf` the SHUFFLE+step handler tracks, or that handler's
incremental erase of the old LCD marker would fire against a position the encoder had
already moved past.

Previews are owned by `ExtPreviewOn`/`ExtPreviewOff`/`ExtPreviewCheck` in key.ino so
that a preview is never left sounding and never held open by `delay()`.

### Velocity levels
Each ext step carries one of two MIDI velocities, chosen per track by
`pattern.extAccent[track]` - a 16-bit word per track, parallel to `extTrack`. Per track
rather than per step: each ext track is an instrument in its own right, so accenting one
must not raise every other track firing on the same step. That costs 32 bytes per pattern
across the bank cache and the four buffers (~640 bytes of RAM); the per-step alternative
would have cost nothing and been unable to express the case.

The two levels are `seq.extVelLow` and `seq.extVelHigh`, set on the ext velocity config
page (below) and defaulting to `MIDI_LOW_VELOCITY` (63) and `MIDI_HIGH_VELOCITY` (111).
TOTAL_ACC adds `MIDI_ACCENT_VELOCITY` on top of whichever level the track already has,
clamped to 127 - it lifts the step rather than flattening the dynamics inside it.

`extAccent` persists in the 32 bytes the stored pattern format already reserved as padding
after `extTrack`, so `PTRN_SIZE` is unchanged. It is stored INVERTED: a pattern written
before this existed has zeros there, and those patterns played at the high level - the only
level the ext lane had - so the complement decodes them as fully accented. Storing the mask
directly would have silently halved the velocity of every existing ext track. Nothing
distinguishes "never written" from a legal all-zero mask otherwise, and no signature byte
would fit in the 32 bytes.

`velocity[EXT_INST][16]`, the shared per-step lane the ext layer used to read, is no longer
read by playback. `InitPattern()` still writes it at the high value so a build predating
this change plays a pattern saved by this one as it did before.

### Config page: ext instrument velocities
SHIFT+TEMPO cycles the config pages; the ext velocity page is the last one
(`CONF_PAGE_EXT_VEL`, 5 with `MIDI_HAS_SYSEX` and 4 without). It shows
`low hi  ext vel` with the two levels below, the encoder button moves between the two
fields, and the encoder sets each in 1..127. The floor is 1, not 0: a note-on with
velocity 0 is a note-off on the wire, so a level of 0 would silence the lane rather than
make it quiet. The two are not ordered against each other - inverting them is a legitimate
way to make the second press the softer of the pair.

The page is appended rather than inserted so the sysex and bootloader handlers, which test
their page number literally, keep the numbers they were written against. `MAX_CONF_PAGE`
is now derived from it, and the page walk in `Seq.ino` is a single increment-and-wrap
rather than an if-chain duplicated in both `#if` branches.

The pair lives in `seq` and persists in the setup EEPROM record (bytes 8-9 of a 64-byte
block with 8 used). No signature is needed to spot a record written before they existed:
those bytes read 0 or 0xFF, neither of which is a legal level, and both fall back to the
compiled-in defaults.

### Output Enable (GUIDE)
GUIDE latches sequenced external MIDI output on and off, replacing the metronome it used
to toggle. `guideBtn.counter` is the latch and also drives `guideLed`. `ServiceExtMidiNotes()`
still latches and clears the queue when output is off, so steps do not pile up and the
note-off pass still runs - only the note-ons are dropped. Unlatching calls
`InitMidiNoteOff()` because the last transmitted step would otherwise stay held on the
external synth with no later note-off to close it, the drain that would have sent one
now being gated off.

The latch is guarded against SHIFT and INST, which qualify GUIDE as the edit-mode enter
and exit gestures. Unguarded it toggled on those too - as the metronome handler did.

Previews and auditions are deliberately not gated: they are a direct response to a button
press rather than sequencer output, so the note map stays audible while editing with
output muted. Output boots unlatched, so every simulator test expecting sequenced notes
arms it first via `latch_guide()`.

The metronome now has no binding. `Metronome()` is uncalled and `metronomeState`
(read by Mux.ino) is never set, so it is permanently off until rebound to something.

### CLEAR and LAST STEP inside edit mode
Both are scoped to the ext layer, because the mode is a layer over the instrument
selection rather than a separate page - the buttons keep their meaning, applied one level
down. CLEAR clears the selected ext track (the step under the playhead while running,
the whole track on SHIFT+CLEAR while stopped) instead of `inst[curInst]`, which in this
mode is `inst[EXT_INST]` - a word the ext playback path never reads, so the old behaviour
muted a voice that drives no circuit and left the track sounding.

LAST STEP sets `pattern.extLength`, not `pattern.length`. Writing the sequencer's own
length from a page that only shows MIDI tracks changed the kit's bar length with no
visible cause. `extLength` lives in the byte the stored format already reserved for it
(EEPROM offset 36, previously written as 0 and skipped), biased by one on the way out so
0 still means "written before this existed" - `extLength` 0 is itself legal, a one-step
ext loop, so the bias is what keeps the full 0-15 range usable. `bufferedPattern` is BSS
until something is copied into it, so `PasteBufferToPattern()` range-checks rather than
copying blind.

`extStepCount` tracks the ext layer's position and wraps on `extLength`, so a shorter ext
length loops the MIDI tracks against the kit instead of truncating the pattern. When the
two lengths are equal - the default, and what every pattern loads as - `extCurStep` is
just `curStep` and behaviour is unchanged. Direction modes are deliberately not
re-derived against `extLength`: BACKWARD/PING_PONG/RANDOM are defined against
`pattern.length`, and recomputing them would make the lanes disagree even when the
lengths match. TOTAL_ACC stays on `curStep` for the same reason it always did - it
accents the whole machine at that moment, so it has to line up with the kit rather than
with the ext loop.

### Playback
`CountPPQN()` records the step into a queue: a track bitmask, an accent mask naming the
tracks that take the high level, the two velocities and a sticky note-off request
(`extPendingOn`, `extPendingAcc`, `extPendingVel`, `extPendingVelAcc`, `extPendingOff`).
The accent is queued as a mask rather than 16 per-track velocities because a step only ever
carries the two levels the editor can program; `ExtTransmitStep()` picks per track. If a step is
overtaken before the queue is drained the newer step wins, which is what the four
consecutive `CountPPQN()` calls per incoming MIDI clock can produce. `InitMidiNoteOff()`
discards a queued but untransmitted step before silencing what is sounding, so stop and
pattern change cannot let notes arrive late or twice. `extSoundingNote[]` records the note
each track actually transmitted, and `SendExtTrackNoteOff()` replays that rather than the
current map entry - retuning a track while it sounds would otherwise note-off a pitch that
was never started.

### Transmission timing
The drum voices are triggered inline in `CountPPQN()`, so anything that delays the ext
notes shows up as the MIDI track playing behind the analog kit. Three separate costs were
measured with `sim/tests/test_ext_latency.c`, which differences the trigger-word write
against the note-on on the wire.

The figure that matters is the LAST ext note of a step, not the first. MIDI is serial, so
a measurement pinned to track 0 reports the best case and hides the spread that a
multi-track pattern is actually heard as - which is why an early version of this test
reported 1.27 ms while the hardware sounded 6 ms late. At 120 BPM:

| ext tracks/step | last note, before | after |
|---|---|---|
| 2 | 2.71 ms | 1.32 ms |
| 6 | 8.37 ms | 4.13 ms |

- **Wire order.** Sending every sounding note-off and then every new note-on put track 0's
  note-on behind `2N+3` bytes at 320 us each, growing with how many tracks the previous
  step left sounding. `ExtTransmitStep()` instead interleaves each track's note-off with
  its own note-on and defers the note-offs of tracks that stop on this step until after
  all the note-ons - releases are not time-critical, the notes are already sounding.
  Note-offs are sent as note-on velocity 0 so running status carries the whole step under
  a single status byte.
- **Scheduling.** Draining only from the loop cost jitter rather than a steady offset: a
  pass that repaints the LCD blocks for ~14 ms, and `needLcdUpdate` is set at end of
  measure, so the worst delay landed on the bar's downbeat. `ServiceExtMidiNotesFromClock()`
  transmits from `CountPPQN()` itself, but only after measuring that `Serial1`'s TX ring
  has room for the step's worst-case burst - the original concern, that `HardwareSerial`
  busy-waits on UDRE with interrupts disabled once the 64-byte ring fills, is respected
  rather than discarded. A denser step than that stays queued and the loop drains it.
- **Note-offs at the step boundary.** With releases interleaved into the step, every
  active track cost 4 bytes there - its note-off then its note-on - so each extra track
  pushed the next track's note-on 1.28 ms further behind the trigger. `extReleaseArmed`
  moves the release to `EXT_RELEASE_LEAD` (2) PPQN ticks *before* the next step, using the
  same boundary expression offset by the lead, so the boundary carries note-ons only at
  2 bytes per track and the releases ride in dead time. `extPendingOff` is still set at
  every step: if the early release could not get out - a very short shuffled gap, or a
  burst the UART could not absorb - the step falls back to the interleaved ordering rather
  than stranding a note.

Running status is load-bearing for all of the above and must not be allowed to depend on
the build. `MySettings` (`downtown-solutions_firmware.ino`) sets `UseRunningStatus` unconditionally,
because it used to sit inside `#if MIDI_HAS_SYSEX` - a flag `features.h` leaves off and
`platformio.ini` forces on. The `#else` branch built the instance from `DefaultSettings`,
where running status is false, so every ext note cost 3 bytes instead of 2 and the
Arduino IDE build documented above silently ran 50% more wire time than the PlatformIO
one. `ServiceExtMidiNotesFromClock()` now budgets `2 * messages + 1` bytes on the
strength of this, so it has to hold everywhere. That budget was `3 * messages`, which
declined steps that would have fit and handed them to the loop, where an end-of-measure
LCD repaint blocks ~14 ms - an order of magnitude more than the wire time it was
protecting. The decline threshold moves from 10 tracks to 15.

The MASTER MIDI clock byte is written straight to `UDR1` near the top of `CountPPQN()`,
and every scale value is a multiple of 4, so `ppqn % 4 == 0` coincides with every
unshuffled step boundary - the clock byte always precedes the step's ext notes. It costs
nothing: roughly 253 us of step computation runs between that write and the trigger
latch, and another ~195 us before the first note byte reaches `UDR1`, by which point the
clock byte finished transmitting at 320 us. Do not "fix" this by moving the clock byte
after the notes; it would delay it by the whole step compute on step ticks only while
leaving intervening clock bytes on time, which is the worst shape of jitter for a slaved
device and a 16x regression of the measured 0.027 ms.

What remains is the wire itself. Under running status a step costs one status byte plus
two per note-on, so the last note of an N-track step cannot arrive sooner than
`(1 + 2N) * 320 us`; the 6-track measurement of 4.13 ms is within 30 us of that floor.
`test_ext_latency.c` therefore budgets against that formula rather than a flat ceiling,
so the bound stays meaningful at any density. Closing the rest would need sub-tick lead
scheduling, and one PPQN tick is 5.2 ms at 120 BPM - too coarse to compensate with, and
tempo-dependent besides.

`extMidiBusy` serialises the two paths: the MIDI library's running-status state,
`HardwareSerial`'s ring and `extTrackNoteOn[]` are all non-reentrant. It is claimed inside
the same `ATOMIC_BLOCK` as the queue latch, because a step queued between latching and
claiming would otherwise be transmitted by the clock path and land ahead of the older step.
The loop claims it only when it actually has something to send, so an empty poll - the
common case once the clock path is doing the work - never locks the clock out.

`test_ext_latency.c` also bounds MASTER MIDI clock jitter (the clock byte is written
straight to UDR1 behind a UDRE busy-wait in the same ISR, so a fuller ring could stall it;
measured 0.027 ms) and asserts that a 16-track step still delivers every track through the
loop fallback without stalling the sequencer.

## SysEx pattern transfer

`Sysex.h` has defined the command set since the code was imported, but
`HandleSystemExclusive()` and `MidiSendSysex()` were empty stubs, so nothing could
ever be read off the machine. Both are implemented now, with `tools/nava` as the
host counterpart.

Messages are `F0 7D 07 1A <cmd> <param> <packed payload> <checksum> F7`. The
bootloader's `7D 08` (see the flashing section) is a different family, so a firmware
page cannot be mistaken for a pattern dump. Payloads are the EEPROM records verbatim
- 448 bytes for a pattern, 1024 for a track, 64 for the setup block - which is what
lets a backup round-trip through a firmware revision that adds fields inside the
padding those records already reserve.

Payloads are 7-in-8 packed (`sysex_pack.h`), not nibblized like the bootloader's.
Nibblizing would double a 1KB track record and push the largest message past what
the MIDI library can reassemble in the RAM left on this board. The checksum covers
the RAW bytes, so mis-unpacking fails instead of storing garbage. `sysex_pack.h`
carries no Arduino dependency precisely so the host test suite can compile it
natively and check it against `tools/nava/protocol.py` in both directions.

Neither direction buffers a whole record. A dump streams out of EEPROM 56 bytes at
a time (a multiple of 7, so a chunk boundary is never mid-group) straight to the
UART; an incoming record is checksummed by indexing the packed bytes in place, then
written a page at a time. The checksum is verified before *any* page is written -
a rejected transfer leaves the stored pattern intact rather than half-replaced.

`SysexResetRunningStatus()` runs after every message. Writing to the UART directly
bypasses the MIDI library's running-status bookkeeping while the `F0..F7` clears the
receiver's, and the two disagreeing would cost the next note-on its status byte.
`MIDI.sendSysEx(0, NULL, true)` emits no bytes and invalidates the stored status.

**The bank cache is the subtle part.** `patternBank[]` is authoritative over EEPROM
until ENTER or a bank change, so `EnableSysexMode()` flushes pending edits via
`FlushPatternBank()` and `DisableSysexMode()` reloads the bank if a host wrote
anything. Without the flush a dump reports stale patterns; without the reload a
restore is silently overwritten the next time the cache is saved.

**Draining the UART is load-bearing.** The MIDI library parses one byte per
`read()` (`Use1ByteParsing`, inherited from `DefaultSettings`) and the loop calls
`read()` once per pass - about 220 bytes/s against the 3125 bytes/s a host sends.
The 64-byte UART ring overran and every restore was lost before the handler saw a
complete message. `loop()` now drains the ring while `seq.SysExMode` is set; that
is the only time a burst this dense arrives, and the sequencer is stopped there.
`sim/tests/test_sysex.c` is what caught this, and pins it.

A dump arrives on the wire with `0xF8` clock bytes sprinkled through it: Timer1 runs
whether or not the sequencer is started, so a MASTER-sync unit clocks continuously.
Real-time bytes are legal anywhere, including between two data bytes of a SysEx
message, and any receiver has to strip them - `sim/tests/test_sysex.c` does, and so
does rtmidi under the CLI.

`MAX_CONF_PAGE`-numbered page 3 is the only place the handler is connected, which is
also why requests are ignored elsewhere. Off that page nothing responds at all.

## Code Heritage & Contributors

The codebase shows contributions from multiple developers:
- **[zabox]**: v1.028 improvements (flam, expander mode, optimization)
- **[Neuromancer]**: MIDI enhancements, SysEx support
- **[oort]**: Pattern bank RAM caching, bug fixes, comments
- **[SIZZLE]**: External instrument edit mode
- **Original author**: Sandor (base firmware)

## Known Issues & TODOs

From code comments:
- Start/Continue mode not fully implemented (Seq.ino:33)
- Pattern groups not saved to EEPROM (Seq.ino:548)
- 9ms DIN start delay not implemented (Seq.ino:122)
- Memory optimization needed (uses ~5.6KB for pattern bank)

Resolved: the external instrument note index (`noteIndexCpt`) is gone along with the
single-note sequencer it belonged to; the 16-track editor addresses steps directly.

Resolved: `HandleSystemExclusive()` and `MidiSendSysex()` are no longer stubs - see
the SysEx section above. Pattern backup needs a PlatformIO build; the Arduino IDE
build leaves `MIDI_HAS_SYSEX` off in `features.h` and compiles no SysEx support.

## Performance Characteristics

- **Main loop**: ~220Hz when sequencer running, reduced to 80Hz with LED smoothing
- **LCD update**: 0.3ms per update (was 1.3ms before optimization)
- **EEPROM writes**: Only on ENTER hold or mode changes (reduces wear)
- **Pattern switching**: Seamless with double-buffering
- **MIDI clock jitter**: Minimized with direct UART register access

## Memory Map (Approximate)

```
RAM (16KB total):
- Pattern buffers:        ~1.5KB (pattern[2] + bufferedPattern + tempPattern, 393 bytes each)
- Pattern bank cache:     ~6.3KB (patternBank[16])
- Track buffers:          ~2KB (track[2])
- Global variables:       ~3KB
Total:                    ~13.5KB of 16KB (82%, measured by the PlatformIO build)

EEPROM (4KB total):
- Patterns (128):         ~57KB needed (doesn't fit!)
- Tracks (16):            ~16KB needed
- Config:                 <1KB
Note: EEPROM storage strategy uses paging/compression
```

## Critical Code Paths

### Pattern Change (sync mode)
1. User selects pattern → `selectedPatternChanged = TRUE`
2. Seq.ino:1123 loads into `pattern[!ptrnBuffer]`
3. Sets `nextPatternReady = TRUE`
4. Clock.ino:170 detects `endMeasure` at last step
5. Swaps `ptrnBuffer = !ptrnBuffer`
6. Sets `curPattern = nextPattern`
7. Seamless transition with no audio glitches

### Step Trigger Path
1. Timer1 ISR calls `CountPPQN()` at PPQN rate
2. Every `pattern[ptrnBuffer].scale` ticks = new step
3. Sets `stepValue` from pattern bits
4. `SetMux()` routes DAC to correct voice
5. `SetDoutTrig()` fires shift register triggers
6. Timer2 ISR fires 2ms later to clear triggers
7. Optional Timer3 ISR for flam delayed hit

### Button Scan Path
1. Main loop calls `ButtonGet()`
2. `ScanDin()` reads 5 SPI shift registers
3. Debouncing in `ButtonGet()` helper
4. State machine detects press/release/hold
5. `SeqParameter()` processes button actions
6. Updates patterns, mode changes, LCD refresh

## Testing & Debugging

### Debug Features
- `#define DEBUG 1` enables serial output
- `MemoryFree.h` for RAM monitoring
- LCD can show debug values via `lcdVal` variable

### Common Debugging Points
- `stepCount` - current step position
- `curPattern` / `nextPattern` - pattern state
- `ptrnBuffer` - which buffer is playing
- `curSeqMode` - current mode
- `isRunning` - sequencer state