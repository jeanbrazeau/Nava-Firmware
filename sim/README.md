# Nava Firmware Simulator

Self-contained cycle-accurate AVR simulation harness for the Nava TR-909
firmware, built on simavr (vendored submodule, native arm64 build).

## Prerequisites

- macOS arm64 (Apple Silicon)
- Xcode Command Line Tools (provides `clang`, `make`)
- PlatformIO with `atmelavr` platform (provides avr-gcc for firmware build)
- `libelf` via Homebrew (keg-only; not on default clang search paths after install):

  ```
  brew install libelf
  ```

Install PlatformIO core if not present:
```
pip install platformio
pio platform install atmelavr
```

## Full Build Sequence

```bash
# 0. Populate the vendored simavr checkout (one-time per clone, needs network).
#    Once sim/simavr is registered as a submodule, this is:
git submodule update --init sim/simavr
#    If it is not registered yet, either register it:
#      git submodule add https://github.com/buserror/simavr.git sim/simavr
#    or just clone it in place:
#      git clone https://github.com/buserror/simavr.git sim/simavr
#    setup_simavr.sh then checks out the pin recorded in simavr.version.

# 1. Build simavr and the HD44780 part (~2 min; requires step 0 and brew libelf)
bash sim/scripts/setup_simavr.sh

# 2. Compile the production firmware ELF from the current working tree
bash sim/scripts/build_firmware.sh

# 3. Build the harness and run all 13 regression tests (TAP output)
make -C sim test
```

No external EEPROM fixture file is required.  Test EEPROM state is seeded
in-process by the C fixtures (`fixtures/patterns.c`) before each test case.

> **Legacy note** — `scripts/gen_eeprom.py` (Python 2) is superseded by the
> in-process C seed path and is not needed for a normal test run.

## Directory Layout

```
sim/
├── harness/        — Peripheral models and sim core (compiled as .o objects)
│   ├── nava_sim.h/c    — Core: ELF load, EEPROM seed, run loop
│   ├── event_log.h/c   — Cycle-stamped observable event log
│   ├── spi_bus.h/c     — 74HC165 input + 74HC595 LED/trig + MCP4822 DAC models
│   ├── gpio.h/c        — MUX attribution, encoder injection, DIN/TRIG-OUT log
│   ├── lcd.h/c         — HD44780 4-bit write-only LCD model (screen text mirror)
│   ├── midi.h/c        — USART1 MIDI TX capture + RX injection
│   └── frontpanel.h/c  — Named button press/release API + observable accessors
├── tests/
│   ├── test_runner.h/c     — TAP test framework + assertion helpers + boot_wait_ready
│   ├── test_timing.c       — PPQN period, Timer2 trigger-off, Timer3 flam, shuffle
│   ├── test_pattern_swap.c — Bar-end SYNC/FREE pattern buffer swap
│   ├── test_ext_inst.c     — Polyphonic EXT_INST MIDI note-on/off + accent bug pin
│   ├── test_midi_sync.c    — SLAVE sync: injected clock advances sequencer
│   └── test_ui.c           — Boot screen, mode transitions, LEDs, tempo/encoder
├── fixtures/
│   └── patterns.h/c        — Reusable C pattern fixtures (all shuffle>=1)
│                               (EEPROM seeded in-process; no .bin file needed)
├── scripts/
│   ├── setup_simavr.sh     — Vendor, pin, and build simavr (requires step 0)
│   ├── build_firmware.sh   — PlatformIO firmware build + mmcu hint
│   └── gen_eeprom.py       — LEGACY/OPTIONAL: Python 2 EEPROM generator
│                               (superseded by in-process C seed; not required)
├── simavr/         — Git submodule (populate: git submodule update --init sim/simavr)
├── simavr.version  — Pinned commit: 6a2c268c2e50a4ef0967f8a7bb281df9eed6c2bb
└── Makefile                — Harness + test build system

```

## Timer Math Reference (bpm=120)

These integer-exact values must match test assertions:

| Parameter | Formula | Result |
|-----------|---------|--------|
| PPQN | 96 | 96 ticks/qn |
| FREQUENCY | (120×96)/60 | 192 Hz |
| OCR1A | (16000000/8)/192 | 10416 (truncating) |
| PPQN tick period | (10416+1)×8 | 83336 cycles |
| 16th-note step (scale=24) | 24×83336 | 2,000,064 cycles |
| Timer2 trigger-off (OCR2A=249, pre=128) | (249+1)×128 | 32,000 cycles = 2ms |
| Timer3 flam[0] (OCR3A=4999, pre=64) | (4999+1)×64 | 320,000 cycles = 20ms |

All three values were read back from the running AVR's OCR1A/OCR2A/OCR3A
registers and match exactly.

### Observed latency offsets

Tests observe the *SPI trigger burst*, timestamped at TRIG_CS deassert — not the
timer tick itself. Each observable therefore carries a small, **constant** offset
that the tolerances absorb. These are fixed offsets, not period errors: a wrong
period would scale with elapsed time, whereas these stay put.

| Measurement | Ideal | Observed | Offset | Cause |
|---|---|---|---|---|
| Step period | 2,000,064 | ~2,000,064 ±100 | ~0 | offsets cancel between two onsets |
| Timer2 trigger-off | 32,000 | ~32,272 | +272 | ISR entry + 2-byte SPI shift-out |
| Timer3 flam | 320,000 | ~321,400 | +1,400 | Timer3 armed *after* the DAC/mux/SPI work |

Note the firmware writes the trigger word **twice per step** (fire, then the
Timer2 restore ~32,000 cycles later). Comparing adjacent TRIG_WORD events
therefore measures the trigger-off window, not the step period — use
`event_log_find_step_onset()`, which skips the restore writes.

## Known Firmware Bugs Pinned by Tests

- **MIDI_ACCENT_VELOCITY=16**: Accent steps produce velocity 16 (quieter than
  typical 111). Pinned in test_ext_inst.c, not fixed.
- **shuffle[-1] OOB**: pattern.shuffle==0 causes `shuffle[(0)-1]` out-of-bounds
  read. All fixtures set shuffle>=1. One quarantined test documents the path.

## Test Harness Gotchas

Two traps cost real debugging time; both are now handled, but know they exist:

- **Wait for panel scanning, not for the LCD.** The firmware writes its boot
  splash (`downtown / solutions 0.91b`) long before it starts polling buttons.
  A press injected during the splash is never sampled, so the sequencer simply
  never starts and every timing assertion fails with "no TRIG_WORD events".
  `boot_wait_ready()` waits for real 74HC165 latches (~46.9M cycles here), which
  is the only signal the firmware can actually see input.
- **A fixture must be able to exercise its assertion.** The shuffle fixture
  originally triggered steps 0,4,8,12 — all even — while the shuffle offset
  applies to *odd* steps, so the swing was unobservable by construction.

## Testing the UI

`test_ui.c` drives the front panel and reads back the LCD and LEDs.

- **Input**: `fp_press_button` / `fp_release_button` (20 named buttons),
  `fp_press_step` (16 step buttons), `nava_gpio_inject_encoder(gpio, ±1, detents)`
  and `nava_gpio_set_encoder_switch`.
- **Output**: `fp_lcd_line(ctx, row)` for the 16×2 screen as text, and
  `fp_step_leds` / `fp_config_leds` / `fp_menu_leds` plus `fp_led_on(ctx, FP_LED_*)`
  for the decoded LED chain.

Four rules make UI tests reliable; ignoring any one of them produces
intermittent failures that look like firmware bugs:

1. **Leave an idle gap before each press.** A press issued immediately after
   the previous release is swallowed by the debounce state machine, and the
   mode change silently never happens.
2. **Poll for the expected state; never read once after a fixed delay.**
   Redraws are slow and non-atomic — a single read can catch a half-updated
   screen mixing old and new text (`" Track Plcl ins "`). Costs vary wildly:
   TRK lands in ~0.5M cycles, PTRN in ~12M because it reloads a pattern over I2C.
3. **LEDs lag the LCD.** They are written on a separate cadence, so a mode LED
   can still be stale when its new screen first appears. Poll the LED too.
4. **Don't assert on blinking LEDs.** `Led.ino:22` composes the stopped state as
   `(LED_PLAY * blinkTempo) | LED_STOP`, so **PLAY blinks while stopped** and
   reads either way depending on when you sample. Use `STOP` to distinguish
   running (clear) from stopped (set).

`TEMPO` is momentary — it shows the tempo only while held and reverts on
release, so read it with the button still down.

Opt-in traces for diagnosing these:

```
make -C sim CFLAGS_EXTRA=-DNAVA_TWI_DEBUG    # EEPROM reads/writes (boot config)
make -C sim CFLAGS_EXTRA=-DNAVA_SPI_DEBUG    # front-panel latches the firmware sees
```

## EEPROM Layout Reference

External I2C EEPROM (24LC1024-style, 128 KiB):

| Region | Offset | Size | Content |
|--------|--------|------|---------|
| Patterns | 0 | 128×448 = 57,344 | 128 patterns × 448 bytes |
| Tracks | 57,344 | 16×1024 = 16,384 | 16 tracks × 1024 bytes |
| Setup | 73,728 | 64 | sync, defaultBpm, channels, flags |

Setup block byte layout (LoadSeqSetup order):
0: sync (0=MASTER), 1: defaultBpm, 2: TXchannel, 3: RXchannel,
4: ptrnChangeSync, 5: muteModeHH, 6: EXTchannel, 7: BootMode
