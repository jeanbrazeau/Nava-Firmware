# TR-909 External Instrument Feature Verification Report

**Date:** 2026-07-26 (supersedes the 2025-12-26 revision)
**Firmware:** Nava Oortone (downtown-solutions_firmware)
**Feature:** TR-909 Style Multi-Track External Instrument Sequencer

## Executive Summary

The 16-track external instrument sequencer is implemented and integrated. The previous
revision of this document declared the feature verified on the strength of static code
analysis alone. That claim did not hold: four subsequent commits found and fixed nine real
defects in code this document had marked as passing, several of them user-visible on the
first press of a button, and one of them introduced by the fix for another. The
verification matrix below therefore separates what has been checked mechanically from what
has only been read.

**Status:** implemented; regression-covered for MIDI note behaviour; not yet validated on
hardware.

### What static analysis missed

The bugs listed here were all present in code the previous revision marked ✅ VERIFIED.
They are recorded so the failure mode is not repeated: reading a code path confirms it
exists, not that it is correct, reachable, or exclusive of the paths around it.

| Defect | Where | Effect |
|---|---|---|
| Input double-handling | Seq.ino step handler vs key.ino | One physical press was consumed twice — as a pattern selection or a legacy `inst[EXT_INST]` edit and again as an `extTrack` toggle. Programming steps while stopped jumped patterns and wrote the toggle into the pattern that had just been swapped out. |
| Mode-state leakage | Seq.ino mode handlers | `extInstEditMode` was never cleared on a mode change, and `curInst` was left stranded on EXT_INST, a voice that drives no drum circuit. The forced selection and the step-LED override also applied in TRACK_PLAY, PTRN_PLAY, PTRN_TAP and MUTE. |
| Blocking delays | Button.ino, key.ino | Mode entry and exit each ran `delay(1000)` inside `ButtonGet()`, and each step audition ran `delay(50)`. `MIDI.read()` is not called during those windows, so under SLAVE and EXPANDER sync incoming clock was dropped and the sequencer stalled. |
| Hanging preview notes | key.ino | The preview note-off was gated on the INST button, so releasing the step button first emitted nothing. Only one note was tracked, so previewing a second track inside one INST hold stranded the first note on the synth. |
| Velocity truncation | Clock.ino / SeqFunc.ino | `InitPattern` seeded `velocity[EXT_INST]` with HIGH_VEL (80), outside the EXT_INST table range of 25..50. `map()` returned 168, the MIDI library masked to 7 bits, and every unaccented note left the machine at 40. |
| Inverted accent | Clock.ino | An accented step was assigned MIDI_ACCENT_VELOCITY (16) — quieter than every unaccented note. |
| ISR-blocking MIDI | Clock.ino | The step handler transmitted up to 96 MIDI bytes from the timer ISR against a 64-byte TX buffer, busy-waiting on UDRE with interrupts disabled. A dense step blocked `CountPPQN()` for roughly 30ms against a PPQN tick of about 5ms, collapsing the MIDI clock and DIN sync generated in the same function. |
| Step programming dead | key.ino vs Button.ino | Programming tested `stepBtn[].justPressed`, whose only live setter is `InstValueGet`. Fixing the input double-handling stopped `SeqParameter` calling it in edit mode, and `ButtonGet` clears the flags every scan, so the flag was always 0 and no step could be programmed at all. The fix that caused this was still correct; it removed the feature's only edge source without replacing it. |
| Preview hung via MUTE | key.ino vs Seq.ino | A sustained track-select preview is released by the step-button release inside the edit block. MUTE assigns `curSeqMode` directly instead of going through `ExitExtInstEditMode()`, so the release was never observed and the note sustained with no way to clear it — `InitMidiNoteOff()` only walks `extTrackNoteOn[]` and `SendAllNoteOff()` targets `seq.TXchannel`. |

---

## Implementation Overview

### Architecture

**Data structure (define.h:409)**
`unsigned int extTrack[16]` replaced `byte extNote[128]` plus `byte extLength`. Each track
is one 16-bit word, one bit per step. Net saving is 97 bytes per pattern; `sizeof(Pattern)`
is 360 bytes, confirmed against the linked ELF.

**MIDI note mapping (define.h:137-142)**
```cpp
// One chromatic note per track. MidiSendNoteOn/Off add 12 before
// transmitting, so these values map to MIDI notes 48 to 63 on the wire.
const byte EXT_TRACK_NOTES[16] PROGMEM = {
  36, 37, 38, 39, 40, 41, 42, 43,  // transmitted as MIDI 48-55
  44, 45, 46, 47, 48, 49, 50, 51   // transmitted as MIDI 56-63
};
```
The table values and the transmitted values differ by the +12 in `MidiSendNoteOn`/`MidiSendNoteOff`
(Midi.ino:84-96). Labels in code and on the LCD cite the transmitted MIDI number rather
than a note name, because the name for a given number depends on the octave convention.

**Global variables (define.h:493-503)**
- `boolean extInstEditMode` — mode flag
- `byte currentExtTrack` — selected track (0-15)
- `byte currentExtNote` — table value for the selected track
- `boolean extInstButtonHandled` — prevents double-triggering within an INST hold
- `boolean extTrackNoteOn[16]` — what is currently sounding; main loop only, never touched from an ISR
- `unsigned int volatile extPendingOn`, `byte volatile extPendingVel`, `boolean volatile extPendingOff` — the clock-to-loop note queue
- `unsigned long extInstSplashUntil` — non-blocking splash deadline
- `byte previewNote`, `boolean previewActive`, `unsigned long previewOffAt` — preview note ownership

**Threading model**
`CountPPQN()` records a step into the queue and transmits nothing (Clock.ino:167-169).
`ServiceExtMidiNotes()` (Midi.ino:51) drains it from `loop()` right after `MIDI.read()` (downtown-solutions_firmware.ino:167),
so under SLAVE sync — where the MIDI parser drives `CountPPQN()` — a queued step goes out with
no added delay. The queue is latched and cleared inside one `ATOMIC_BLOCK`; transmission
happens outside it.

---

## Feature Verification Matrix

Status legend: **TESTED** — covered by an automated regression test; **READ** — confirmed by
reading the code, not exercised; **HW** — requires hardware.

| # | Feature | File:Line | Status | Notes |
|---|---------|-----------|--------|-------|
| 1 | Mode entry/exit | Button.ino:59-77, key.ino:98-104 | READ | SHIFT+GUIDE toggles, INST+GUIDE exits; both route through `ExitExtInstEditMode()` |
| 2 | Mode-state teardown | key.ino:9-19, Seq.ino:130,149,166,206,223 | READ | TRK, PTRN, TAP and config entry clear the flag and restore `curInst = BD`. MUTE does neither by design — see below |
| 3 | Track selection (1-16) | key.ino:114-132 | READ | INST + step selects track, preview sustains until step release. Edge-detected on `prevExtStepState`, so re-tapping the same track with INST still held selects it again |
| 4 | Step programming | key.ino:140-158 | TESTED | `ext_inst_step_programming_via_panel` drives the panel and asserts the resulting note. Toggles bits in `extTrack[currentExtTrack]`; adding a step auditions for 50ms unless the running pattern already uses the track |
| 5 | Preview note ownership | key.ino:21-73, 95-96, 136 | READ | One note at a time. Four release paths: step release (136), the timeout in `ExtPreviewCheck()` (69-73), the context guard for modes that leave PTRN_STEP without clearing the flag (95-96), and `ExitExtInstEditMode()` (16) |
| 6 | Polyphonic note-on | Clock.ino:134-169, Midi.ino:51-81 | TESTED | `ext_inst_polyphonic_note_on` |
| 7 | Note-off at next step | Midi.ino:16-32 | TESTED | `ext_inst_note_off_at_next_step` |
| 8 | Accent velocity | Clock.ino:159-163 | TESTED | `ext_inst_accent_velocity` asserts accent is louder, not quieter |
| 9 | Channel separation | Clock.ino:150-165 | TESTED | `ext_inst_no_drum_notes_in_midi` |
| 10 | Queue discard on stop/pattern change | Midi.ino:35-43 | READ | `InitMidiNoteOff()` clears the queue before silencing what sounds |
| 11 | Step buttons not shared with pattern select | Seq.ino:525 | READ | Handler stands down while ext edit mode owns the step buttons |
| 12 | LED feedback | Led.ino:38-46, 123-135 | READ | GUIDE LED blinks in mode; step LEDs show the selected track, scoped to PTRN_STEP |
| 13 | LCD track display | LCD.ino:247-250 | READ | Shows "T 1".."T16" in place of the instrument name |
| 14 | Non-blocking splash | Button.ino:71-77, LCD.ino:14-46 | READ | 800ms deadline, painted once per deadline so a re-arm inside the window repaints, one forced redraw on expiry |
| 15 | EEPROM save | EEprom.ino:87-98 | READ | 16 extTrack words (32 bytes) plus page padding |
| 16 | EEPROM load | EEprom.ino:156-163, 220-227 | READ | Matching read for pattern and pattern-bank paths |
| 17 | Pattern copy | SeqFunc.ino:220-223 | READ | |
| 18 | Pattern paste | SeqFunc.ino:243-246 | READ | |
| 19 | Pattern clear | Seq.ino:440-443 | READ | SHIFT+CLEAR zeroes all 16 tracks |

Row 13 of the previous revision verified a "Keyboard Mode Block" at key.ino:31-52. That code
no longer exists: the single-note keyboard mode it guarded was removed, and with it the
`keyboardMode` flag, `keybOct`, `noteIndex`, `noteIndexCpt`, and the `nameOct`/`nameNote`
string tables. The entry point in key.ino is now `ExtInstUpdate()`. The file keeps its name
because .ino files are concatenated in name order and renaming it would reorder definitions.

---

## Regression Coverage

`sim/` holds a simavr-based harness that boots the real firmware ELF and drives it through
the emulated panel and MIDI ports. `cd sim && make test` runs 18 tests across 5 binaries.
Five cover this feature directly, in `sim/tests/test_ext_inst.c`:

| Test | Asserts |
|---|---|
| `ext_inst_step_programming_via_panel` | SHIFT+GUIDE enters edit mode, a bare step press programs the step, and the sequencer sounds it |
| `ext_inst_polyphonic_note_on` | Multiple tracks programmed on one step all emit note-on |
| `ext_inst_note_off_at_next_step` | The previous step's notes are silenced before the next step's note-ons |
| `ext_inst_accent_velocity` | An accented step is louder than an unaccented one |
| `ext_inst_no_drum_notes_in_midi` | Drum voices do not leak onto the external channel |

The accent test previously pinned the buggy value as expected behaviour; it now asserts the
corrected one. The panel test is the first to exercise the edit-mode UI: it was written
against the broken firmware, confirmed failing, and only then made to pass. The four
MIDI-level tests could not have caught the dead-programming defect, because they seed
`extTrack[]` through the EEPROM fixture and never press a step button.

**Still not covered by the suite:** track selection, preview note lifecycle, LED feedback,
the splash, and mode-state teardown — including the MUTE path that stranded a preview note.
Those rows are marked READ above. The lesson from the defect table stands: every defect
found so far was in the untested UI surface, and one round of tests has not changed that for
the rows still marked READ.

---

## Known Limitations

1. **Fixed note range:** 16 chromatic notes, MIDI 48 to 63 as transmitted
2. **Step length:** all tracks share the main pattern's 16-step length
3. **Velocity:** one velocity per step, shared by every track firing on it
4. **Pattern groups:** external track data is not included in group save/load (groups are not fully implemented in this firmware)
5. **Queue depth:** one step. A step overtaken before the loop drains it is coalesced away; this is intentional, since its notes were about to be cut by the newer step anyway and the note-off request is sticky
6. **External notes inherit main-loop stalls that drum triggers do not.** Triggers fire from the clock ISR; external MIDI is queued there and transmitted from `loop()`. Anything that blocks the loop therefore drops queued external steps while the drums stay in time — a pattern bank load (Seq.ino:531) or a bank save (Seq.ino:1108, tens of `delay(DELAY_WR)` page writes) are the realistic cases. Accepted: the alternative is transmitting from the ISR, which is the defect this design replaced
7. **A pattern change drops one in-flight external step.** `InitMidiNoteOff()` discards the queue on every `selectedPatternChanged`, including SYNC mode, so selecting a pattern mid-bar loses at most one step of external notes. Accepted: the discard is what stops notes arriving after a stop or sounding twice
8. **No preview while the sequencer owns the track.** `ExtPreviewOn()` declines when the running pattern has any step programmed on the selected track, because preview and sequencer share the note and channel with no ownership arbitration and would cut each other off. The pitch is audible from the sequencer anyway. This applies to track selection and to the on-add audition alike; the audition is issued before the bit is set, so adding the first step to an empty track while running still sounds, and only later additions to that track are silent

---

## Hardware Testing Required

Static analysis and the simavr suite cannot substitute for the following. The UI items
matter most: that is where the defects were.

1. **Mode entry/exit**
   - [ ] SHIFT + GUIDE enters and exits; splash appears and clears without freezing the display
   - [ ] Sequencer keeps running and MIDI clock keeps flowing across the splash, under SLAVE sync
   - [ ] Leaving the mode lands on BD, and step buttons drive a real voice again
   - [ ] GUIDE LED blinks while in the mode

2. **Track selection**
   - [ ] INST + step 1-16 selects tracks; LCD shows T 1 through T16
   - [ ] Preview sounds the right pitch and stops on release, including when INST is released first
   - [ ] Selecting a second track inside one INST hold leaves no note sounding

3. **Step programming**
   - [ ] Step buttons toggle steps for the selected track and nothing else
   - [ ] Programming while stopped does not change the current pattern
   - [ ] Audition on add, silence on remove

4. **Mode isolation**
   - [ ] TRACK_PLAY, PTRN_PLAY, PTRN_TAP and MUTE show their own step LEDs and allow normal instrument selection after leaving ext edit mode

5. **Playback**
   - [ ] Multiple tracks trigger simultaneously
   - [ ] Unaccented notes sound near the top of the range, accented ones louder still
   - [ ] Dense steps do not disturb tempo, MIDI clock or DIN sync

6. **Pattern operations and persistence**
   - [ ] SHIFT + CLEAR clears all external tracks
   - [ ] Copy and paste carry external tracks
   - [ ] External track data survives EEPROM save, load and bank change

7. **No stuck notes**
   - [ ] On STOP, on pattern change, on mode exit, on track switch
   - [ ] Hold INST + a step to preview, press MUTE, then release: the note must stop

---

## Conclusion

The feature is implemented, its MIDI note behaviour is pinned by automated regression
tests, and one edit-mode path — entering the mode and programming a step — is now driven
through the emulated front panel. The rest of the user interface is still untested and has
been the source of every defect found so far, including one introduced by the fix for
another. Hardware validation is required before this can be called verified, and the UI
checks above should be run first.

---

**Report revised:** 2026-07-26
**Firmware Version:** Nava Oortone (downtown-solutions_firmware)
