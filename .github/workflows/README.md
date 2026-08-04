# .github/workflows/

## Overview

One workflow does real work: `release.yml` turns a version tag into a published
`.syx` that `nava tui` can download and flash. The other is inherited and inert.

## Design decisions

### The build happens on a runner, not on a laptop

Nothing about the compile requires CI. Publishing from a runner is what makes the
artifact traceable: the image people flash came from a named commit and a log
anyone can read, rather than from whatever was in someone's working tree at the
time. That is also why `scripts/release.py` builds nothing itself - pushing the
tag is the entire trigger.

### The tag is checked against the splash before anything is built

The first step compares `$GITHUB_REF_NAME` against `FIRMWARE_VERSION` in
`version.h` and fails the run if they disagree. The panel is the only version a
user standing in front of the machine can read, so a release labelled 0.92 whose
firmware prints 0.91b is worse than no release at all. `release.py` guards the
same thing locally; this check exists because that one runs on a machine somebody
is watching and this one does not.

### The image is decoded before it is published

The post-build conversion is the one step between a good compile and a file that
bricks a unit, and nothing downstream would notice a malformed one until someone
flashed it. So the workflow decodes its own `.syx` back to flash bytes with
`nava.bootloader` before uploading it.

### nava-tools is installed next to PlatformIO

Not into a separate environment. A pip-installed PlatformIO runs `extra_scripts`
under the same interpreter, and `convert_to_sysex.py` imports `nava` from there;
installing it anywhere else would leave the post-action unable to find it. The
same step apt-installs `libasound2-dev`, because `python-rtmidi` builds from
source on an interpreter it has no wheel for, and that build wants a MIDI
backend's headers. Nothing in CI sends MIDI - this is the price of installing the
package whole rather than reasoning about which dependency each step reaches.

### main.yml is kept, not fixed

It came from the upstream BenZonneveld project and has never run in this fork; the
note in `../about workflows.txt` is the original owner saying so. It is
`workflow_dispatch` only, so it cannot fire on its own, and it refers to a
`Nava2021/` tree and a Python 2 converter that are not in this repository. It is
left in place as provenance rather than repaired into a second build path that
nobody would keep working.

## Invariants

- The tag pattern is a glob, not a regex. GitHub does not accept `+` here, and a
  pattern written as a regex silently matches nothing at all - the release would
  simply never run.
- The release notes are built through a quoted heredoc and substituted into a
  second file. In-place `sed -i` needs an argument on BSD and rejects one on GNU,
  so a line that works on the runner could not be run by hand on a Mac.
