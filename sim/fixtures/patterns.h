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
 * An accented step sends MIDI_HIGH_VELOCITY + MIDI_ACCENT_VELOCITY = 111 + 16. */
#define FX_EXT_WIRE_BASE    48u   /* 36 + 12 transpose */
#define FX_EXT_WIRE(track)  ((uint8_t)(FX_EXT_WIRE_BASE + (track)))
#define FX_MIDI_ACCENT_VEL  127u  /* accent adds on top of the nominal high velocity */

/* The two levels an ext step can be programmed at (Clock.ino: MIDI_HIGH_VELOCITY and
 * MIDI_LOW_VELOCITY).  TOTAL_ACC adds MIDI_ACCENT_VELOCITY=16 on top of either. */
#define FX_MIDI_HIGH_VEL     111u
#define FX_MIDI_LOW_VEL       63u
#define FX_MIDI_LOW_ACC_VEL   79u  /* unaccented track on a TOTAL_ACC step */

/* Config page carrying the two ext velocity levels, counted in TEMPO presses from
 * outside config mode.  Matches CONF_PAGE_EXT_VEL (define.h) for a MIDI_HAS_SYSEX
 * build, which is what platformio.ini produces: 1 setup, 2 setup, 3 sysex, 4
 * bootloader, 5 ext velocity. */
#define FX_CONF_PAGE_EXT_VEL   5

/* Pattern fixture data structure (mirrors firmware's Pattern struct fields
 * needed to build EEPROM images and inject via I2C EEPROM seed). */
typedef struct {
    uint8_t  length;        /* 0-15 (steps-1) */
    uint8_t  scale;         /* PPQN ticks per step */
    uint8_t  shuffle;       /* 1-7; NEVER 0 (avoids OOB) */
    uint8_t  flam;          /* 0-7 */
    uint16_t inst[16];      /* step bitmask per instrument */
    uint16_t extTrack[16];  /* step bitmask per EXT track */
    /* Second velocity level, per step per track: set = FX_MIDI_HIGH_VEL, clear =
     * FX_MIDI_LOW_VEL.  Stored inverted on the wire to EEPROM (see
     * fx_pattern_to_eeprom), so a fixture leaving this zero does NOT reproduce a
     * legacy image — 0xFFFF does. */
    uint16_t extAccent[16];
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

/* EXT_INST pattern: extTrack[0] and extTrack[3] active on steps 0,8, both fully
 * accented; TOTAL_ACC on step 8 (pinned bug test). Its accent words serialize to
 * zeros, so it is also a byte-exact image of a pattern saved before ext steps had
 * two velocity levels. */
extern const fx_pattern_t FX_PTRN_EXT;

/* Both velocity levels live on one step across two tracks: track 0 accented and
 * track 3 unaccented on step 0, track 0 unaccented again on step 8. */
extern const fx_pattern_t FX_PTRN_EXT_LEVELS;

/* BD and ext tracks 0/3 on every step: lets a test measure, per step, the delay
 * between the analog trigger CountPPQN() writes inline and the ext note-on the
 * loop transmits. */
extern const fx_pattern_t FX_PTRN_EXT_SYNC;

/* All 16 ext tracks on every step: exceeds the UART TX ring, so the clock-side
 * transmit must decline and the loop must still deliver every step. */
extern const fx_pattern_t FX_PTRN_EXT_DENSE;

/* Six ext tracks on every step - a realistic multi-track arrangement, and the case
 * a single-track measurement hides: the sixth note-on is serialised behind the other
 * five, so this is what the user actually hears as "the MIDI is late". */
extern const fx_pattern_t FX_PTRN_EXT_BUSY;

/* Two-pattern group (used in pattern-swap test):
 * FX_PTRN_GROUP_A: BD on all steps (produces non-zero trig word)
 * FX_PTRN_GROUP_B: SD on all steps (different trig word, distinct from A) */
extern const fx_pattern_t FX_PTRN_GROUP_A;
extern const fx_pattern_t FX_PTRN_GROUP_B;

#endif /* NAVA_FIXTURES_PATTERNS_H */
