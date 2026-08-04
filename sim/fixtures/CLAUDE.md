# sim/fixtures/

Pattern and setup records seeded into the simulated EEPROM in-process, before
each case. No fixture file on disk is involved.

## Files

| File | What | When to read |
| ---- | ---- | ------------ |
| `patterns.h` / `patterns.c` | The `FX_*` fixtures; all set `shuffle >= 1` | Needing a different starting pattern. A fixture must be able to exercise its assertion - see `../README.md` |
