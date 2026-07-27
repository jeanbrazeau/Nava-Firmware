/* sim/fixtures/patterns.h — CI-071
 * Reusable C pattern fixtures for timing, swap, EXT_INST, and shuffle tests.
 * ALL fixtures set pattern.shuffle >= 1 (avoids shuffle[-1] OOB, DL-015). */
#ifndef NAVA_FIXTURES_PATTERNS_H
#define NAVA_FIXTURES_PATTERNS_H

#include <stdint.h>
#include <stddef.h>

/* Instrument bit constants matching define.h (values are bit positions) */
#define FX_BD   8
#define FX_SD   9
#define FX_LT   10
#define FX_MT   11
#define FX_HT   2
#define FX_RM   3
#define FX_HC   4
#define FX_CH   14
#define FX_TOTAL_ACC 12
#define FX_EXT_INST  13

/* Scale values (PPQN ticks per step) */
#define FX_SCALE_16  24   /* SCALE_16  = PPQN/4 */
#define FX_SCALE_32  12   /* SCALE_32  = PPQN/8 */

/* EEPROM layout constants (from EEprom.ino) */
#define FX_PTRN_SIZE    448u
#define FX_MAX_PTRN     128u
#define FX_TRACK_SIZE  1024u
#define FX_MAX_TRACK     16u
#define FX_OFFSET_SETUP (FX_PTRN_SIZE * FX_MAX_PTRN + FX_TRACK_SIZE * FX_MAX_TRACK)
#define FX_EEPROM_SIZE  (128u * 1024u)

/* MIDI note constants for EXT_INST.
 * EXT_TRACK_NOTES[track] = 36+track (wire = note+12 = 48..63 = 0x30..0x3F).
 * MIDI_ACCENT_VELOCITY = 16 (pinned bug, DL-015). */
#define FX_EXT_WIRE_BASE    48u   /* 36 + 12 transpose */
#define FX_EXT_WIRE(track)  ((uint8_t)(FX_EXT_WIRE_BASE + (track)))
#define FX_MIDI_ACCENT_VEL  16u   /* pinned bug: quieter than expected */

/* Pattern fixture data structure (mirrors firmware's Pattern struct fields
 * needed to build EEPROM images and inject via I2C EEPROM seed). */
typedef struct {
    uint8_t  length;        /* 0-15 (steps-1) */
    uint8_t  scale;         /* PPQN ticks per step */
    uint8_t  shuffle;       /* 1-7; NEVER 0 (avoids OOB) */
    uint8_t  flam;          /* 0-7 */
    uint16_t inst[16];      /* step bitmask per instrument */
    uint16_t extTrack[16];  /* step bitmask per EXT track */
    uint8_t  velocity[16][16]; /* per-instrument per-step velocity */
    uint8_t  groupPos;
    uint8_t  groupLength;
    uint8_t  totalAcc;
} fx_pattern_t;

/* Serialize fx_pattern_t to a 448-byte EEPROM block (same layout as
 * SavePattern writes).  dst must have room for FX_PTRN_SIZE bytes. */
void fx_pattern_to_eeprom(const fx_pattern_t *p, uint8_t *dst);

/* Build a complete FX_EEPROM_SIZE EEPROM image with bpm=120, sync=MASTER,
 * and one custom pattern at slot 0; all other slots use the default fixture.
 * Caller must free() the returned pointer. */
uint8_t *fx_make_eeprom_image(const fx_pattern_t *pattern0,
                               uint8_t ext_channel,
                               uint8_t ptrn_change_sync);

/* ---- Pre-built fixture instances ---- */

/* 16-step, all-BD pattern, no shuffle (shuffle=1), no flam */
extern const fx_pattern_t FX_PTRN_BASIC;

/* 16-step, BD on steps 0,4,8,12; SD on steps 4,12; shuffle=2 */
extern const fx_pattern_t FX_PTRN_SHUFFLED;

/* 16-step, BD with flam on steps 0,4; flam type 0 (20ms) */
extern const fx_pattern_t FX_PTRN_FLAM;

/* EXT_INST pattern: extTrack[0] and extTrack[3] active on steps 0,8;
 * EXT_INST velocity set to HIGH_VEL; accent on step 8 (pinned bug test). */
extern const fx_pattern_t FX_PTRN_EXT;

/* Two-pattern group (used in pattern-swap test):
 * FX_PTRN_GROUP_A: BD on all steps (produces non-zero trig word)
 * FX_PTRN_GROUP_B: SD on all steps (different trig word, distinct from A) */
extern const fx_pattern_t FX_PTRN_GROUP_A;
extern const fx_pattern_t FX_PTRN_GROUP_B;

#endif /* NAVA_FIXTURES_PATTERNS_H */
