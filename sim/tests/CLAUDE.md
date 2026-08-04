# sim/tests/

One binary per `test_*.c`, each linked against the harness and the pinned
firmware ELF. TAP output. `test_runner.c` is the framework and has no `main()`.

## Files

| File | What | When to read |
| ---- | ---- | ------------ |
| `test_runner.h` / `test_runner.c` | TAP framework, assertion helpers, `boot_wait_ready()` | Adding a test file; changing the boot budget |
| `test_timing.c` | PPQN step period, Timer2 trigger-off, Timer3 flam, shuffle offset, and the `shuffle == 0` clamp | Touching Clock.ino or timer.ino; any change to the step trigger path |
| `test_ext_inst.c` | Polyphonic ext note-on/off, the two velocity levels, TOTAL_ACC | Changing ext playback or the accent model |
| `test_ext_latency.c` | Trigger-to-note-on latency for the LAST note of a step, MIDI clock jitter, 16-track loop fallback | Changing ext transmission timing, wire order or the TX-ring budget |
| `test_step_edit.c` | PTRN_STEP and EXT INST playheads, step programming, a pattern with zeroed setup bytes | Changing step editing, playhead rendering or `SanitizePattern()` |
| `test_pattern_swap.c` | Bar-end pattern buffer swap in SYNC and FREE | Changing pattern change or the double buffer |
| `test_midi_sync.c` | SLAVE sync: injected clock advances the sequencer | Changing clock input handling |
| `test_sysex.c` | Pattern/track/config dump and restore, including real-time bytes mixed into a message and the UART drain | Changing the SysEx handler or record sizes |
| `test_eeprom_init.c` | The PLAY+STOP -> PLAY+ENTER wipe, asserting all 128 records come back in range | Changing `InitEEprom()` or the stored defaults |
| `test_ui.c` | Boot dissolve, splash, mode changes, LEDs, tempo | Changing any screen or LED; read `../README.md` first - four rules make these reliable |
