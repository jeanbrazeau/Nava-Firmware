# Nava Firmware Simulator

Self-contained cycle-accurate AVR simulation harness for the Nava TR-909
firmware, built on simavr (vendored submodule, native arm64 build).

Setup and the build sequence are in `CLAUDE.md` beside this file; what follows is
what the code and the test output do not tell you.

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

## Firmware behaviour these tests pin

Both entries here were once bugs the tests documented rather than caught. They
are fixed; the tests now assert the fix, which is why they are worth naming - a
regression would look like a return to behaviour someone might mistake for
intended.

- **Accent is louder, not quieter.** `TOTAL_ACC` adds `MIDI_ACCENT_VELOCITY` (16)
  on top of the level a track already carries, clamped at 127, rather than
  replacing it. An accented step on a high-velocity track sends 127.
  `test_ext_accent_velocity` in `test_ext_inst.c`.
- **`shuffle == 0` no longer indexes out of bounds.** `shuffle[pattern.shuffle - 1]`
  would read `shuffle[-1]`; `SanitizePattern()` now clamps the field on every path
  that loads a stored record, and `test_shuffle_zero_is_clamped` in `test_timing.c`
  times an unshuffled bar to prove it. Fixtures still set `shuffle >= 1`.

## Test Harness Gotchas

Two traps cost real debugging time; both are now handled, but know they exist:

- **Wait for panel scanning, not for the LCD.** The firmware writes its boot
  splash (`downtown / solutions <version>`) long before it starts polling buttons.
  A press injected during the splash is never sampled, so the sequencer simply
  never starts and every timing assertion fails with "no TRIG_WORD events".
  `boot_wait_ready()` waits for real 74HC165 latches (~67M cycles here, most of
  it the boot animation and splash), which is the only signal the firmware can
  actually see input.
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
- **Raw screen bytes**: `fp_lcd_raw(ctx, row)` returns the 16 DDRAM bytes
  unsubstituted. `fp_lcd_line` renders every non-ASCII code as `.`, which cannot
  tell the all-dots-on block (`0xFF`) from a custom glyph (codes 0-7) — the boot
  dissolve animation is made entirely of that distinction. The test also counts
  how many cells hold a custom glyph at once, which is how many are mid-fill;
  the text mirror cannot express that at all. Note that the HD44780 model stores
  CGRAM at the same addresses as row 0 (`0x00-0x3F`), so while glyphs are being
  written row 0's mirror is scribbled over; row 1 sits at `0x40` and is always
  trustworthy.

Boot now costs ~4.2 s of simulated time — ~1.26 s of panel dissolve animation, then
the 2 s version splash — and the panel is not scanned until both finish. That is
what `BOOT_CYCLES` (96M) budgets for; a boot budget below it fails every test in
the file at once with "firmware not polling the front panel".

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
