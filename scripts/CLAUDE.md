# scripts/

What is left of the host side after the `nava` tools moved to
[jeanbrazeau/nava-tools](https://github.com/jeanbrazeau/nava-tools): the release
script, and the checks that need the firmware sources in hand.

## Files

| File | What | When to read |
| ---- | ---- | ------------ |
| `README.md` | Why a release refuses rather than improvises; why these checks live here and not with the tool they compare against | Changing the release guards; deciding where a new check belongs |
| `release.py` | Rewrites `FIRMWARE_VERSION`, commits, tags, pushes. Standard library only | Cutting a release; changing what blocks one |
| `pyproject.toml` | pytest config and the dev group, including the `nava-tools` git dependency | Adding a check; changing how the host package is resolved |

## Subdirectories

| Directory | What | When to read |
| --------- | ---- | ------------ |
| `tests/` | The three checks that need firmware sources | Adding a check on the firmware headers; a protocol number that disagrees |

## Release

```bash
python3 scripts/release.py 0.92 --dry-run
python3 scripts/release.py 0.92
```

## Test

```bash
cd scripts && uv run pytest
# without uv: pip install pytest "git+https://github.com/jeanbrazeau/nava-tools"
```

`uv.lock` is deliberately gitignored here - see `README.md`.
