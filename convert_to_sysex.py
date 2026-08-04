"""PlatformIO post-action: turn the built .hex into a bootloader .syx.

Calls nava in process rather than shelling out to the old Python 2
hex2sysex.py, which cannot run on a current interpreter - the build used to
report "SysEx conversion failed" and carry on to a successful-looking finish
with no .syx produced.

nava is a separate package now (jeanbrazeau/nava-tools), so this is an import
of something that may not be installed. It says what is missing and how to get
it rather than dying on an ImportError traceback out of a SCons callback, where
nothing on screen would point at the cause.

The install line quotes THIS interpreter by name because it is rarely the one
`pip` resolves to: PlatformIO installed from its own script runs extra_scripts
under ~/.platformio/penv, and a nava installed anywhere else is invisible here.
"""

import sys

Import("env")  # SCons

try:
    from nava import bootloader, ihex
except ImportError as exc:  # pragma: no cover - exercised by the build, not pytest
    raise SystemExit(
        "SysEx conversion needs the nava tools, which are not installed for this "
        "Python ({}).\n"
        "    {} -m pip install 'git+https://github.com/jeanbrazeau/nava-tools'\n"
        "The build stops here rather than after compiling: a .syx that is "
        "missing goes unnoticed until the unit is already in bootloader "
        "mode.".format(exc, sys.executable)
    )


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
