# scripts/tests/

Checks that need the firmware sources. Run with `cd scripts && uv run pytest`.

## Files

| File | What | When to read |
| ---- | ---- | ------------ |
| `test_firmware_constants.py` | Parses `define.h`, `Sysex.h`, `EEprom.ino` and compares the protocol numbers, record sizes and buffer sizing against `nava.protocol` | Changing a SysEx command, a record size, or `SYSEX_BUFFER_SIZE`; a dump that reads the wrong record |
| `test_sysex_pack.py` | Compiles `sysex_pack.h` natively and drives it through ctypes against the host packer, both directions | Changing 7-in-8 packing; skips when no C compiler is present |
| `test_release.py` | Cuts releases in throwaway git repositories with real remotes, covering each guard | Changing `release.py`; adding a condition that should block a release |
