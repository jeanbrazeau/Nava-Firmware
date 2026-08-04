# sim/scripts/

## Files

| File | What | When to read |
| ---- | ---- | ------------ |
| `setup_simavr.sh` | Checks out the pin in `simavr.version` and builds simavr plus the HD44780 part | First-time setup; changing the simavr revision |
| `build_firmware.sh` | PlatformIO firmware build plus the mmcu hint the harness needs | Rebuilding the ELF under test - required before `make test` |
| `gen_eeprom.py` | Python 2 EEPROM generator, superseded by the in-process C seed | Never run - kept for reference only |
