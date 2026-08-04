# scripts/

## Overview

Two jobs that both need the firmware sources, which is the only reason they did
not leave with the rest of the host code: cutting a release from `version.h`, and
proving that the firmware's protocol numbers still agree with the host tool that
speaks to them.

## Design decisions

### The version lives in exactly one file

`downtown-solutions_firmware/version.h` is the only place the number is written
down. The splash prints `FIRMWARE_VERSION`, `release.py` rewrites it, and
`.github/workflows/release.yml` refuses to publish a tag that disagrees with it.
The panel is the only version a user standing in front of the machine can read,
so a release whose number differs from the splash is worse than no release. The
header also fails to compile if the version is too long for the 16-column splash
line, so the same constraint is enforced at three points on purpose - the last
one runs on a machine nobody is watching.

### It refuses rather than improvises

A release is a tag other people flash from, so it cannot be re-cut. `release.py`
stops on:

- uncommitted changes to tracked files - the tag would name a commit that does
  not contain the work, and the build comes from the commit. Untracked files do
  not block anything, since they cannot reach the tag or the build; they are
  listed so a source file that was never `git add`ed is not silently left out.
  Blocking on them would also invite `git stash -u` as the fix, which would sweep
  away the worktrees under `.claude/`
- a branch other than `master`, unless `--branch` says otherwise
- a tag that already exists locally or on the remote
- a version that is not `<digits>.<digits>` with an optional trailing letter, or
  is too long for the display

Nothing is pushed until everything local has succeeded, and the branch is pushed
before the tag: a tag whose commit is not on the remote builds from a commit
nobody can see.

### Standard library only

`release.py` imports nothing outside the standard library, and is run as
`python3 scripts/release.py` rather than through an environment. A release must
not be blocked on installing anything - including on the host package, which it
does not need, because it builds and publishes nothing itself. Pushing the tag is
the whole trigger.

### The header checks live here, not with the tool

`test_firmware_constants.py` and `test_sysex_pack.py` compare the firmware
against `nava.protocol` in the other repository. Either side could host them. They
are here because the firmware headers are the half that cannot be fetched: a
check that needs `define.h`, `Sysex.h` and `sysex_pack.h` has to run where those
files are, and installing a Python package to get the other half is the cheap
direction.

The comparison is symmetric even though its location is not. A number that
disagrees is a bug on whichever side moved last, and this is the only place it
surfaces - nothing in either suite alone would catch it. On hardware it would
present as a backup that silently reads the wrong EEPROM record.

### The lockfile is not committed

A committed `scripts/uv.lock` would pin `nava-tools` to one commit, which is
exactly the drift these checks exist to report: they would keep passing against a
host package that no longer matches the headers beside them. The dependency is
declared against the repository, not a version, so the pair tracks master.

## Invariants

- `release.py` must keep `VERSION_FILE` pointing at the file the firmware
  actually includes. `test_release.py` asserts this against the real tree, so
  renaming one without the other fails rather than silently bumping nothing.
- `test_sysex_pack.py` skips when no C compiler is present rather than failing,
  because it is the only check here that needs one. A skip in CI on a machine
  without `cc` is not a passing protocol check - read the summary, not the exit
  code.
- These tests do not run in this repository's CI. The release workflow is the
  only workflow that runs on its own, and it runs on a tag.
