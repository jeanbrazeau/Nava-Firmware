#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""sim/scripts/gen_eeprom.py — CI-070
Generate sim/fixtures/eeprom_default.bin from a documented struct map.

Layout derived from EEprom.ino / define.h:
  PTRN_SIZE        = 448  bytes per pattern
  MAX_PTRN         = 128  patterns
  TRACK_SIZE       = 1024 bytes per track
  MAX_TRACK        = 16   tracks
  OFFSET_SETUP     = PTRN_SIZE*MAX_PTRN + TRACK_SIZE*MAX_TRACK = 73728

Setup block at OFFSET_SETUP (LoadSeqSetup byte order):
  [0] sync           = 0 (MASTER)
  [1] defaultBpm     = 120
  [2] TXchannel      = 1
  [3] RXchannel      = 1
  [4] ptrnChangeSync = 1 (SYNC mode)
  [5] muteModeHH     = 1
  [6] EXTchannel     = 2   (MIDI_EXT_CHANNEL=1 compiled in)
  [7] BootMode       = 3   (PTRN_STEP, CONFIG_BOOTMODE=1 compiled in)

Pattern layout per pattern (relative offsets, 448 bytes total):
  0:   inst[16]          = 32 bytes (2 bytes each, low then high)
  32:  length,scale,shuffle,flam,unused,groupPos,groupLength,totalAcc,
       unused[24]        = 32 bytes
  64:  extTrack[16]      = 32 bytes + 32 padding
  128: second compat page= 64 bytes (all zero)
  192: velocity[0..15][0..15] = 256 bytes (4 pages × 64 bytes)

All patterns initialised with:
  length=15, scale=24 (SCALE_16=PPQN/4), shuffle=1 (avoids shuffle[-1] OOB),
  flam=0, others=0.  All steps/velocities zero.

Python 2.7-compatible (consistent with repo tooling: hex2sysex uses python 2.7).
"""
from __future__ import print_function
import os
import struct
import sys

# ---- constants (from define.h / EEprom.ino) ----
PTRN_SIZE   = 448
MAX_PTRN    = 128
TRACK_SIZE  = 1024
MAX_TRACK   = 16
OFFSET_SETUP = PTRN_SIZE * MAX_PTRN + TRACK_SIZE * MAX_TRACK   # 73728
SETUP_SIZE  = 64

EEPROM_SIZE = 128 * 1024    # 24LC1024 = 128 KiB

# Sequencer config defaults
MASTER      = 0
DEFAULT_BPM = 120
PTRN_STEP   = 3   # SeqMode enum value

# Pattern defaults
DEFAULT_LEN   = 15   # length field = steps-1 = 15 for 16-step
DEFAULT_SCALE = 24   # SCALE_16 = PPQN/4 = 24
DEFAULT_SHUF  = 1    # avoid shuffle[-1] OOB (DL-015)
DEFAULT_FLAM  = 0


def make_pattern(length=DEFAULT_LEN, scale=DEFAULT_SCALE,
                 shuffle=DEFAULT_SHUF, flam=DEFAULT_FLAM,
                 inst=None, exttrack=None, velocity=None):
    """Return 448-byte pattern block.

    inst:     list of 16 unsigned ints (step bitmasks per instrument)
    exttrack: list of 16 unsigned ints (step bitmasks per EXT track)
    velocity: 16x16 list of bytes
    """
    buf = bytearray(PTRN_SIZE)

    # ---- inst[16]: 32 bytes at offset 0, little-endian uint16 ----
    off = 0
    for i in range(16):
        val = inst[i] if inst else 0
        struct.pack_into('<H', buf, off + i * 2, val & 0xFFFF)
    off = 32

    # ---- setup: 32 bytes at offset 32 ----
    buf[off + 0] = length  & 0xFF
    buf[off + 1] = scale   & 0xFF
    buf[off + 2] = shuffle & 0xFF
    buf[off + 3] = flam    & 0xFF
    buf[off + 4] = 0       # old extLength (unused)
    buf[off + 5] = 0       # groupPos
    buf[off + 6] = 0       # groupLength
    buf[off + 7] = 0       # totalAcc
    # bytes [8..31] = 24 unused parameter bytes (already zero)

    # ---- extTrack[16]: 32 bytes at offset 64, little-endian uint16 ----
    off = 64
    for i in range(16):
        val = exttrack[i] if exttrack else 0
        struct.pack_into('<H', buf, off + i * 2, val & 0xFFFF)
    # bytes [96..127] = 32 padding (already zero)

    # second compat page [128..191] = all zero (already zero)

    # ---- velocity[16][16]: 256 bytes at offset 192 ----
    off = 192
    for inst_i in range(16):
        for step_j in range(16):
            v = 0
            if velocity and inst_i < len(velocity) and step_j < len(velocity[inst_i]):
                v = velocity[inst_i][step_j]
            buf[off + inst_i * 16 + step_j] = v & 0xFF

    return bytes(buf)


def make_eeprom():
    eeprom = bytearray(b'\xff' * EEPROM_SIZE)

    # ---- Pattern bank: 128 default patterns ----
    for p in range(MAX_PTRN):
        pdata = make_pattern()
        offset = p * PTRN_SIZE
        eeprom[offset:offset + PTRN_SIZE] = bytearray(pdata)

    # ---- Track bank: 16 empty tracks (all 0xFF already) ----
    # END_OF_TRACK = 128; pattern[0] in each track = 0xFF = end marker by default

    # ---- Setup block at OFFSET_SETUP ----
    setup = bytearray(SETUP_SIZE)
    setup[0] = MASTER       # sync
    setup[1] = DEFAULT_BPM  # defaultBpm = 120
    setup[2] = 1            # TXchannel
    setup[3] = 1            # RXchannel
    setup[4] = 1            # ptrnChangeSync = SYNC
    setup[5] = 1            # muteModeHH
    setup[6] = 2            # EXTchannel (MIDI_EXT_CHANNEL compiled in)
    setup[7] = PTRN_STEP    # BootMode (CONFIG_BOOTMODE compiled in)
    # [8..63] = unused, left as 0x00

    eeprom[OFFSET_SETUP:OFFSET_SETUP + SETUP_SIZE] = setup

    return bytes(eeprom)


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    fixtures_dir = os.path.join(script_dir, '..', 'fixtures')
    if not os.path.isdir(fixtures_dir):
        os.makedirs(fixtures_dir)
    out_path = os.path.join(fixtures_dir, 'eeprom_default.bin')

    eeprom = make_eeprom()
    with open(out_path, 'wb') as f:
        f.write(eeprom)

    print("Generated: %s (%d bytes)" % (out_path, len(eeprom)))
    print("  OFFSET_SETUP = %d (0x%X)" % (OFFSET_SETUP, OFFSET_SETUP))
    print("  bpm=120, sync=MASTER, ptrnChangeSync=SYNC, EXTchannel=2")
    print("  All %d patterns: shuffle=%d, scale=%d, length=%d" % (
        MAX_PTRN, DEFAULT_SHUF, DEFAULT_SCALE, DEFAULT_LEN + 1))


if __name__ == '__main__':
    main()
