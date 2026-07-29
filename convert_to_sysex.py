"""PlatformIO post-action: turn the built .hex into a bootloader .syx.

Calls tools/nava in process rather than shelling out to the old Python 2
hex2sysex.py, which cannot run on a current interpreter - the build used to
report "SysEx conversion failed" and carry on to a successful-looking finish
with no .syx produced.
"""

import os
import sys

Import("env")  # SCons

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))

from nava import bootloader, ihex  # noqa: E402 - needs the path set above


def generate_sysex(source, target, env):
    hex_file = str(target[0])
    syx_file = hex_file[: -len(".hex")] + ".syx" if hex_file.endswith(".hex") else hex_file + ".syx"

    try:
        image = ihex.load_file(hex_file)
        stream = bootloader.encode_firmware(image)
        with open(syx_file, "wb") as handle:
            handle.write(stream)
    except (OSError, ValueError, ihex.HexFileError) as exc:
        # Fail the build. A missing .syx that goes unnoticed until the unit is
        # already in bootloader mode is the worse outcome.
        raise SystemExit("SysEx conversion failed: {}".format(exc))

    pages = len(stream.split(b"\xf7")) - 2  # minus the reset message and the tail
    print("\nSysEx file created: {} ({} bytes of flash in {} pages)\n".format(
        syx_file, len(image), pages))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", generate_sysex)
