# Nava Oortone Firmware

Alternate firmware for the Nava, a hardware replica of the Roland TR-909
(ATmega1284p @ 16MHz).

## Files

| File | What | When to read |
| ---- | ---- | ------------ |
| `README.md` | User-facing manual: flashing, bootloader entry, config pages, backups | Explaining the machine to someone holding one; checking panel behaviour |
| `platformio.ini` | The only build definition: env `nava_sysex`, build flags, the post-action hook | Changing build flags, lib_deps, upload protocol |
| `convert_to_sysex.py` | PlatformIO post-action: built `.hex` -> bootloader `.syx`. Imports `nava` | Debugging a build that produces no `.syx` |
| `TR909_EXT_INST_VERIFICATION.md` | Verification report for the 16-track ext instrument feature | Checking what was verified how, and what the earlier revision got wrong |
| `migrate_to_platformio.sh` | One-shot Arduino-IDE -> PlatformIO migration, superseded by the symlink layout below | Historical only; it hardcodes an absolute path and copies sources - do not run |
| `Nava0tone_0.90b.syx.zip` | The released 0.90b image | Never edit - it is the fixture the host encoder is pinned against |
| `.gitmodules` | Registers `sim/simavr` | Setting up the simulator in a fresh clone |
| `.gitignore` | Build output, `.venv`, and `scripts/uv.lock` | Adding generated output; the lockfile exclusion is deliberate - see `scripts/README.md` |

## Subdirectories

| Directory | What | When to read |
| --------- | ---- | ------------ |
| `downtown-solutions_firmware/` | The firmware sources, and the design record beside them | Any firmware change; understanding the sequencer, EXT_INST, SysEx or timing |
| `sim/` | simavr harness and the C regression tests | Adding or debugging a test; reproducing timing behaviour without hardware |
| `scripts/` | `release.py`, and the checks that need the firmware sources | Cutting a release; a protocol number that disagrees with the host tool |
| `.github/` | Release workflow, triggered by a version tag | Changing what a release publishes or verifies |
| `src` | Symlink to `downtown-solutions_firmware/`, which is how PlatformIO finds the sources without a second copy | Never edit - it is a link, not a directory |

## Build

```bash
pio run -e nava_sysex          # emits .pio/build/nava_sysex/firmware.{hex,syx}
```

The `.syx` comes from the post-action, which imports `nava` - install it first:
`pip install "git+https://github.com/jeanbrazeau/nava-tools"`.

On Apple Silicon the AVR toolchain is x86-only (neither PlatformIO nor Arduino
publishes an arm64 build), so a build needs Rosetta:

```bash
softwareupdate --install-rosetta --agree-to-license
```

For an Arduino IDE build instead: open `downtown-solutions_firmware.ino` in 2.0.4,
board ATmega1284, processor ATmega1284 (16MHz), programmer USBasp or AVRISP mkII.
That build leaves `MIDI_HAS_SYSEX` off, so it compiles no SysEx support. Convert
its `.hex` by hand:

```bash
nava hex2syx path_to_hex_file.hex -o output.syx
nava flash output.syx --out NAVA-909
```

## Test

```bash
cd sim && make test            # C regression tests; see sim/CLAUDE.md for setup
cd scripts && uv run pytest    # host-side checks against the firmware headers
```

Run `pio run` before `make test` or the suite tests a stale ELF.

## Release

```bash
python3 scripts/release.py 0.92 --dry-run
python3 scripts/release.py 0.92
```

Pushing the tag is the whole trigger; CI builds, verifies and publishes the
`.syx`. See `scripts/README.md` for the guards and why the version lives in one
file.

## Development

The `nava` CLI and TUI live in a separate repository,
[jeanbrazeau/nava-tools](https://github.com/jeanbrazeau/nava-tools); `nava backup`
and `nava restore` move patterns over SysEx while the unit sits on the SysEx
config page.

Two things here import it: `convert_to_sysex.py` and the release workflow. Both
name the package and the interpreter in their failure messages.
