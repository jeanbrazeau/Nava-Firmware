/* sim/tests/test_runner.h — CI-081
 * TAP test framework and cycle-exact assertion helpers.
 *
 * Timer math at bpm=120 (from nava_sim.h macros):
 *   OCR1A       = 10416  (truncating integer division)
 *   PPQN period = (10416+1)*8 = 83336 cycles
 *   Step period = 24 * 83336 = 2000064 cycles  (scale_16)
 *   Trig-off    = (249+1)*128 = 32000 cycles    (Timer2)
 *   Flam[k]     = (flam[k]+1)*64 cycles         (Timer3)
 *
 * All timing assertions use these integer values with a tolerance to absorb
 * ISR entry latency (typically <=10 cycles on the ATmega). */
#ifndef NAVA_TEST_RUNNER_H
#define NAVA_TEST_RUNNER_H

#include <stdint.h>
#include <stddef.h>
#include "nava_sim.h"
/* Tests dereference ctx->avr->cycle and call avr_run() directly, so they need
 * the full struct avr_t — nava_sim.h only forward-declares it. */
#include "sim_avr.h"
#include "event_log.h"
#include "patterns.h"

/* ---- Test registration ---- */
typedef void (*test_fn_t)(nava_sim_t *ctx);

typedef struct {
    const char *name;
    test_fn_t   fn;
    /* Optional: path to EEPROM fixture.  NULL → use default (bpm=120, MASTER) */
    const uint8_t *eeprom_image;
    size_t         eeprom_size;
    /* If non-NULL, overwrite EEPROM pattern slot 0 with this fixture */
    const fx_pattern_t *pattern0;
    uint8_t  ext_channel;         /* EXTchannel for MIDI EXT_INST tests */
    uint8_t  ptrn_change_sync;    /* 1=SYNC, 0=FREE */
} test_entry_t;

/* Register a test.  Use the macro for convenience. */
void test_register(const test_entry_t *entry);

/* Run all registered tests.  Returns number of failures (0 = all passed). */
int test_run_all(const char *elf_path);

/* Macro for simple tests (no special fixture) */
#define TEST(name_str, fn_ptr) \
    do { \
        static const test_entry_t _e = { \
            .name = name_str, .fn = fn_ptr, \
            .eeprom_image = NULL, .eeprom_size = 0, \
            .pattern0 = NULL, .ext_channel = 2, .ptrn_change_sync = 1 \
        }; \
        test_register(&_e); \
    } while (0)

/* Macro for tests with a custom pattern fixture in slot 0 */
#define TEST_WITH_PATTERN(name_str, fn_ptr, ptrn_ptr, extch, sync) \
    do { \
        static const test_entry_t _e = { \
            .name = name_str, .fn = fn_ptr, \
            .eeprom_image = NULL, .eeprom_size = 0, \
            .pattern0 = (ptrn_ptr), .ext_channel = (extch), \
            .ptrn_change_sync = (sync) \
        }; \
        test_register(&_e); \
    } while (0)

/* ---- Assertion helpers ---- */

/* Cycle-exact equality with tolerance for ISR entry jitter */
#define CYCLE_TOLERANCE 32ULL    /* 32 cycles ≈ 2 µs at 16 MHz */

/* Assert actual is within [expected-tol, expected+tol].
 * On failure: print diagnostic and mark test failed (non-fatal — suite continues). */
void assert_cycle_within(const char *label,
                          uint64_t actual, uint64_t expected, uint64_t tolerance);

/* Assert exact cycle match (no tolerance) */
void assert_cycle_eq(const char *label, uint64_t actual, uint64_t expected);

/* Assert the 16-bit trigger word equals expected */
void assert_trig_word(const char *label,
                       const sim_event_t *evt, uint16_t expected_word);

/* Assert a MIDI note-on was observed; returns the event (or fails) */
const sim_event_t *assert_midi_note_on(const char *label,
                                        const event_log_t *log,
                                        uint8_t channel, uint8_t wire_note,
                                        uint64_t cycle_lo, uint64_t cycle_hi);

/* Assert a MIDI note-off was observed */
const sim_event_t *assert_midi_note_off(const char *label,
                                         const event_log_t *log,
                                         uint8_t channel, uint8_t wire_note,
                                         uint64_t cycle_lo, uint64_t cycle_hi);

/* Assert LCD line (row 0 or 1) contains the given substring */
void assert_lcd_contains(const char *label,
                          const nava_sim_t *ctx, int row,
                          const char *substr);

/* Mark current test as failed with a message (non-fatal) */
void test_fail(const char *label, const char *fmt, ...);

/* Get current test's failure count */
int test_current_failures(void);

/* Step the firmware up to max_cycles, stopping early once LCD row 0 shows
 * content (proves the firmware reached its run loop after EEPROM boot read).
 * Emits a TAP diagnostic if the LCD stays blank for the full budget.
 * Returns number of cycles actually consumed. */
uint64_t boot_wait_ready(nava_sim_t *ctx, uint64_t max_cycles);

/* Return the nth (0-based) STEP-ONSET trigger-word event at or after `from`,
 * or NULL.  The firmware emits TWO TRIG_WORD writes per step: the instrument
 * fire, then the Timer2 restore ~NAVA_TRIG_OFF_CYCLES later that clears it.
 * Naively taking consecutive TRIG_WORD events therefore measures the 32000-cycle
 * trigger-off window instead of the ~2000064-cycle step period.  This skips the
 * restore writes so callers get real step onsets. */
const sim_event_t *event_log_find_step_onset(const event_log_t *log,
                                              uint64_t from, int n);

#endif /* NAVA_TEST_RUNNER_H */
