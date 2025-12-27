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

- `jjjjj_firmware/`: Main firmware directory containing Arduino code
  - `jjjjj_firmware.ino`: Main entry point for the Arduino firmware
  - Various `.ino` files for different functional components (Button, Clock, Dio, etc.)
  - `define.h`: Contains all definitions, constants, and global variables
  - `features.h`: Feature toggles and configuration options
  - `src/`: External libraries
    - `MemoryFree/`: Memory management utilities
    - `SPI/`: SPI communication library
    - `WireN/`: I2C communication library (custom)
- `tools/`: Python utilities for firmware deployment
  - `hex2sysex/`: Converts compiled hex files to MIDI SysEx format
  - `hexfile/`: Utilities for working with hex files
  - `midi/`: MIDI file manipulation libraries

## Development Commands

### Firmware Compilation and Upload

The firmware is developed using Arduino IDE. To compile and upload:

1. Open the jjjjj_firmware.ino file in Arduino IDE 2.0.4 (recommended version)
2. Select the appropriate board settings:
   - Board: ATmega1284
   - Processor: ATmega1284 (16MHz)
   - Programmer: USBasp or AVRISP mkII (depending on your hardware)
3. Compile the firmware using the Arduino IDE

### Converting Firmware to SysEx for Upload

After compiling, to convert the .hex file to SysEx format for uploading to Nava:

```bash
cd /Users/admin/offline/Nava-Firmware
python tools/hex2sysex/hex2sysex.py --syx --output_file output.syx path_to_hex_file.hex
```

Note: This repository uses Python 2.7.x for the conversion tools.

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
2. Polls buttons and encoders for user input
3. Updates LEDs, LCD, and sequencer parameters
4. Manages sequencer configuration and keyboard updates

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

## File Structure (jjjjj_firmware/)

### Main Entry Point
- **jjjjj_firmware.ino** (206 lines)
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
    - Manages MIDI note on/off for external instruments
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
- **key.ino** - Keyboard mode for external instrument note entry

#### Output Handling
- **Led.ino** - LED control via shift registers
- **LCD.ino** - 16x2 LCD display updates for all modes
- **Dio.ino** - Digital I/O via SPI shift registers
- **Mux.ino** - Multiplexer control for routing triggers to drum voices

#### Hardware Interface
- **EEprom.ino** - Pattern/track storage and retrieval
- **Midi.ino** - MIDI in/out, note handling, clock sync
- **Expander.ino** - Expander mode (trigger-to-MIDI conversion)

### Configuration Files
- **define.h** (625 lines) - All constants, structures, global variables
- **features.h** (11 lines) - Feature flags (MIDI_BANK_PATTERN_CHANGE, MIDI_EXT_CHANNEL, etc.)
- **function_declarations.h** - Forward declarations for cross-file function calls
- **Sysex.h** - SysEx bootloader support
- **string.h** - Custom LCD character definitions

## Data Structures Deep Dive

### Pattern Structure (457 bytes each)
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
  byte extNote[128];        // MIDI note numbers for external instrument sequencer
  byte extLength;           // Number of notes in external instrument sequence
  byte groupPos;            // Position in pattern group/chain
  byte groupLength;         // Length of pattern group/chain
  byte totalAcc;            // Total accent track
};
```

### Memory Management Strategy
- **pattern[2]**: Twin buffers - edit one (`!ptrnBuffer`) while playing other (`ptrnBuffer`)
- **patternBank[16]**: Current bank cached in RAM (16 patterns × 457 bytes = 7.3KB)
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
  byte configPage;         // Current config page (0-4)
  boolean configMode;      // In config mode flag
  boolean setupNeedSaved;  // Config needs EEPROM save
  boolean muteModeHH;      // Hi-hat mute mode (link CH/OH)
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

## External Instrument (EXT_INST) - SIZZLE Firmware Extension

### Feature Overview
- Full MIDI note sequencer on dedicated track
- Each of 16 steps can have different MIDI note
- Velocity control per step
- Separate MIDI channel (`seq.EXTchannel`)

### Edit Mode Activation
- SHIFT + GUIDE toggles EXT_INST edit mode
- Forces `curInst = EXT_INST`
- Display shows: "EXT INST EDIT ON / NOTE: C3"

### Note Entry
- Uses `extNote[128]` array in Pattern structure
- Each step stores MIDI note number (0-127)
- Keyboard mode for note entry (octave selection)

### Playback
Located in Clock.ino:196-152:
```cpp
if (bitRead(pattern[ptrnBuffer].inst[EXT_INST], curStep)) {
  MidiSendNoteOn(seq.EXTchannel, pattern[ptrnBuffer].extNote[curStep], velocity);
  midiNoteOnActive = TRUE;
}
```

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
- External instrument note index handling needs revision (Clock.ino:172)
- Pattern groups not saved to EEPROM (Seq.ino:548)
- 9ms DIN start delay not implemented (Seq.ino:122)
- Memory optimization needed (uses ~7KB for pattern bank)

## Performance Characteristics

- **Main loop**: ~220Hz when sequencer running, reduced to 80Hz with LED smoothing
- **LCD update**: 0.3ms per update (was 1.3ms before optimization)
- **EEPROM writes**: Only on ENTER hold or mode changes (reduces wear)
- **Pattern switching**: Seamless with double-buffering
- **MIDI clock jitter**: Minimized with direct UART register access

## Memory Map (Approximate)

```
RAM (16KB total):
- Pattern buffers:        ~5KB (pattern[2] + buffers)
- Pattern bank cache:     ~7KB (patternBank[16])
- Track buffers:          ~2KB (track[2])
- Global variables:       ~2KB
Total:                    ~16KB (very tight!)

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