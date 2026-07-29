#pragma once

// 7-in-8 SysEx payload packing.
//
// Deliberately free of Arduino dependencies: this is the part of the transfer
// that is easiest to get subtly wrong and hardest to notice - a dropped high bit
// corrupts a velocity's flam flag rather than producing anything that looks like
// an error - so tools/tests/test_sysex_pack.py compiles this header natively and
// checks it against the host implementation in tools/nava/protocol.py.
//
// A group is 7 raw bytes sent as 8: one byte carrying their high bits, then the
// 7 bytes with those bits stripped. Bit k of the leading byte belongs to the k-th
// byte of the group.

#include <stdint.h>

#define SYSEX_GROUP_RAW    7
#define SYSEX_GROUP_PACKED 8

// High bits of up to 7 raw bytes, as the leading byte of a packed group.
static inline uint8_t SysexPackMsbs(const uint8_t* group, uint8_t count) {
  uint8_t msbs = 0;
  for (uint8_t i = 0; i < count; i++) msbs |= (uint8_t)((group[i] >> 7) << i);
  return msbs;
}

// Raw byte `index` of a packed payload, without unpacking the whole record.
static inline uint8_t SysexUnpackByteAt(const uint8_t* packed, unsigned int index) {
  const uint8_t* base = packed + (index / SYSEX_GROUP_RAW) * SYSEX_GROUP_PACKED;
  uint8_t offset = (uint8_t)(index % SYSEX_GROUP_RAW);
  return (uint8_t)(base[1 + offset] | (uint8_t)(((base[0] >> offset) & 1) << 7));
}

// Wire size of a payload, and the raw size a wire payload decodes to. A trailing
// partial group is 1 + n bytes, so the two are not a plain ratio.
static inline unsigned int SysexPackedLen(unsigned int rawLen) {
  unsigned int rest = rawLen % SYSEX_GROUP_RAW;
  return (rawLen / SYSEX_GROUP_RAW) * SYSEX_GROUP_PACKED + (rest ? rest + 1 : 0);
}

static inline unsigned int SysexUnpackedLen(unsigned int packedLen) {
  unsigned int rest = packedLen % SYSEX_GROUP_PACKED;
  return (packedLen / SYSEX_GROUP_PACKED) * SYSEX_GROUP_RAW + (rest ? rest - 1 : 0);
}
