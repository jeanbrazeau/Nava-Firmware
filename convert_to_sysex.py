Import("env")  # SCon
import os

def generate_sysex(source, target, env):
    hex_file = str(target[0])
    syx_file = hex_file.replace('.hex', '.syx')

    # Call existing conversion script
    cmd = 'python tools/hex2sysex/hex2sysex.py --syx --output_file {} {}'.format(syx_file, hex_file)
    print("\nConverting to SysEx: {}".format(cmd))
    result = os.system(cmd)

    if result == 0:
        print("SysEx file created: {}\n".format(syx_file))
    else:
        print("SysEx conversion failed\n")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", generate_sysex)
