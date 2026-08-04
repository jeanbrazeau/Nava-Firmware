# sim/

Cycle-accurate AVR simulation harness for the firmware, built on simavr. Runs the
production ELF, so a test exercises the same binary a unit would be flashed with.

## Files

| File | What | When to read |
| ---- | ---- | ------------ |
| `README.md` | Timer math, observed latency offsets, harness traps, UI-testing rules, EEPROM layout | Before writing a test; when a test fails intermittently or a timing number looks wrong |
| `Makefile` | Builds the harness and one binary per `tests/test_*.c`; TAP output | Adding a test file, changing build flags, enabling a debug trace |
| `PLAN.json` | Original build-out plan for the harness | Historical only |
| `simavr.version` | Pinned simavr commit | Changing the simavr revision |
| `.gitignore` | Ignores `build/` and the vendored simavr tree | - |

## Subdirectories

| Directory | What | When to read |
| --------- | ---- | ------------ |
| `harness/` | Peripheral models: SPI chain, LCD, MIDI, GPIO, front panel, event log | Observing something the tests cannot see yet; adding a model |
| `tests/` | The regression tests, one binary each | Adding coverage; finding which test pins a behaviour |
| `fixtures/` | C pattern fixtures, seeded into EEPROM in-process | Needing a different starting pattern or setup record |
| `scripts/` | simavr setup, firmware build | First-time setup; rebuilding the ELF under test |
| `simavr/` | Vendored submodule at the pinned commit | Never edit directly - `git submodule update --init sim/simavr` |

## Prerequisites

macOS arm64, Xcode Command Line Tools, PlatformIO with the `atmelavr` platform,
and `libelf` (keg-only, so not on clang's default search paths):

```bash
brew install libelf
pip install platformio && pio platform install atmelavr
```

## Build and test

```bash
git submodule update --init sim/simavr   # one-time per clone, needs network
bash sim/scripts/setup_simavr.sh         # builds simavr + the HD44780 part, ~2 min
bash sim/scripts/build_firmware.sh       # compiles the ELF from the working tree
make -C sim test                         # TAP output
```

Step 3 is not optional before step 4: `make test` links whatever ELF is on disk,
so skipping the rebuild tests the previous revision of the firmware.

No EEPROM fixture file is needed - state is seeded in-process from
`fixtures/patterns.c` before each case.

## Debug traces

```bash
make -C sim CFLAGS_EXTRA=-DNAVA_TWI_DEBUG    # EEPROM reads/writes (boot config)
make -C sim CFLAGS_EXTRA=-DNAVA_SPI_DEBUG    # front-panel latches the firmware sees
```
