#pragma once

#define START_OF_SYSEX      (char)0xF0
#define END_OF_SYSEX        0xF7

#define SYSEX_MANUFACTURER  0x7d
// The next two give us "909" bitshifted to the left to fit into 7 bits of MIDI data.

#define SYSEX_DEVID_1 0x07
#define SYSEX_DEVID_2 0x1A

// F0 + manufacturer + two device bytes + command + parameter.
#define HEADERSIZE      6
// Sysex Commands:
#define NAVA_BANK_DMP   0x00
#define NAVA_PTRN_DMP   0x01
#define NAVA_TRACK_DMP  0x02
#define NAVA_CONFIG_DMP 0x03
#define NAVA_LEVELS_DMP 0x04
#define NAVA_FULL_DMP   0x05

// Request command's
#define NAVA_BANK_REQ   0x40
#define NAVA_PTRN_REQ   0x41
#define NAVA_TRACK_REQ  0x42
#define NAVA_CONFIG_REQ 0x43
#define NAVA_LEVELS_REQ 0x44
#define NAVA_FULL_REQ   0x45
#define NAVA_FBANK_REQ  0x46
#define NAVA_FTRACK_REQ 0x47
#define NAVA_ACK        0x48

// ACK parameter: how the last incoming message was handled. The host retries on
// anything but OK, so these have to separate "lost in transit" from "the device
// will never accept this" - see request_dump() in tools/nava/midiio.py.
#define NAVA_ACK_OK           0
#define NAVA_ACK_BAD_CHECKSUM 1
#define NAVA_ACK_BAD_LENGTH   2
#define NAVA_ACK_BAD_PARAM    3
#define NAVA_ACK_BUSY         4

// Payload records are the EEPROM images from EEprom.ino, dumped verbatim. A
// backup then round-trips through any firmware revision that adds fields inside
// the padding those records already reserve, without this file knowing about it.
#define SYSEX_PTRN_BYTES   448  // PTRN_SIZE
#define SYSEX_TRACK_BYTES  1024 // TRACK_SIZE
#define SYSEX_CONFIG_BYTES 64   // SETUP_SIZE

// Payload bytes are 7-in-8 packed: 7 raw bytes become 8 MIDI data bytes, the
// first carrying their high bits. Nibblization - what the bootloader uses - would
// have doubled the 1KB track record and pushed the largest message well past what
// the MIDI library can reassemble in the RAM this board has left.
#define SYSEX_PACKED(raw)   (((raw) / 7) * 8 + ((raw) % 7 ? (raw) % 7 + 1 : 0))
#define SYSEX_MSG_SIZE(raw) (HEADERSIZE + SYSEX_PACKED(raw) + 2)  // + checksum + F7

#define SYSEX_PTRN_SIZE   SYSEX_MSG_SIZE(SYSEX_PTRN_BYTES)   // 520
#define SYSEX_TRACK_SIZE  SYSEX_MSG_SIZE(SYSEX_TRACK_BYTES)  // 1179
#define SYSEX_CONFIG_SIZE SYSEX_MSG_SIZE(SYSEX_CONFIG_BYTES) // 82

// The largest message that must arrive whole. The MIDI library reserves this much
// RAM for reassembly and splits anything longer into fragments, so it is sized to
// the track record - the largest thing a host ever sends this device.
#ifndef SYSEX_BUFFER_SIZE
#define SYSEX_BUFFER_SIZE SYSEX_TRACK_SIZE
#endif

// A bank is dumped as its 16 pattern messages rather than one large one, so the
// buffer above is set by the track record alone and BANK_PARTS-style chunking is
// not needed.
