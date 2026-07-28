/* sim/tests/test_runner.c — CI-080
 * TAP-emitting test framework with per-test fresh AVR + event log context. */
#include "test_runner.h"
#include "nava_sim.h"
#include "lcd.h"
#include "midi.h"
#include "frontpanel.h"
#include "spi_bus.h"
#include "patterns.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <assert.h>

/* ELF path injected by the Makefile via -DNAVA_ELF_PATH=... */
#ifndef NAVA_ELF_PATH
#define NAVA_ELF_PATH "../.pio/build/nava_sysex/firmware.elf"
#endif

#define MAX_TESTS 128

static test_entry_t g_tests[MAX_TESTS];
static int          g_n_tests    = 0;
static int          g_test_ok    = 1;   /* 1 = current test passing */
static int          g_test_fails = 0;   /* failures in current test */

void test_register(const test_entry_t *entry) {
    assert(g_n_tests < MAX_TESTS);
    g_tests[g_n_tests++] = *entry;
}

int test_current_failures(void) { return g_test_fails; }

/* ---- TAP output helpers ---- */
static void tap_ok(int n, const char *name) {
    printf("ok %d - %s\n", n, name);
}
static void tap_not_ok(int n, const char *name) {
    printf("not ok %d - %s\n", n, name);
}
static void tap_diag(const char *fmt, ...) {
    printf("# ");
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
}

/* ---- Assertion implementations ---- */

void test_fail(const char *label, const char *fmt, ...) {
    g_test_ok    = 0;
    g_test_fails++;
    tap_diag("FAIL [%s]", label);
    if (fmt) {
        char msg[512];
        va_list ap; va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);
        tap_diag("  %s", msg);
    }
}

void assert_cycle_within(const char *label,
                          uint64_t actual, uint64_t expected, uint64_t tol) {
    uint64_t lo = expected >= tol ? expected - tol : 0;
    uint64_t hi = expected + tol;
    if (actual < lo || actual > hi) {
        test_fail(label,
                  "cycle: actual=%llu expected=%llu tol=%llu "
                  "(delta=%lld)",
                  (unsigned long long)actual,
                  (unsigned long long)expected,
                  (unsigned long long)tol,
                  (long long)actual - (long long)expected);
    }
}

void assert_cycle_eq(const char *label, uint64_t actual, uint64_t expected) {
    assert_cycle_within(label, actual, expected, 0);
}

void assert_trig_word(const char *label,
                       const sim_event_t *evt, uint16_t expected) {
    if (!evt) {
        test_fail(label, "no TRIG_WORD event found");
        return;
    }
    if (evt->trig_word != expected) {
        test_fail(label, "trig_word: actual=0x%04X expected=0x%04X",
                  evt->trig_word, expected);
    }
}

const sim_event_t *assert_midi_note_on(const char *label,
                                        const event_log_t *log,
                                        uint8_t channel, uint8_t wire_note,
                                        uint64_t lo, uint64_t hi) {
    const sim_event_t *e = nava_midi_expect_note_on(log, channel, wire_note,
                                                     lo, hi);
    if (!e) {
        test_fail(label,
                  "no MIDI note-on: ch=%u wire_note=0x%02X window=[%llu,%llu]",
                  channel, wire_note,
                  (unsigned long long)lo, (unsigned long long)hi);
    }
    return e;
}

const sim_event_t *assert_midi_note_off(const char *label,
                                         const event_log_t *log,
                                         uint8_t channel, uint8_t wire_note,
                                         uint64_t lo, uint64_t hi) {
    const sim_event_t *e = nava_midi_expect_note_off(log, channel, wire_note,
                                                      lo, hi);
    if (!e) {
        test_fail(label,
                  "no MIDI note-off: ch=%u wire_note=0x%02X window=[%llu,%llu]",
                  channel, wire_note,
                  (unsigned long long)lo, (unsigned long long)hi);
    }
    return e;
}

void assert_lcd_contains(const char *label,
                          const nava_sim_t *ctx, int row,
                          const char *substr) {
    const char *line = nava_lcd_get_line(ctx->lcd, row);
    if (!line) {
        test_fail(label, "LCD row %d: no data yet", row);
        return;
    }
    if (!strstr(line, substr)) {
        test_fail(label, "LCD row %d: \"%s\" not found in \"%s\"",
                  row, substr, line);
    }
}

/* ---- Test execution ---- */

/* Number of completed front-panel scans that proves the main loop is live.
 * ScanDin latches twice per pass (debounce), so a handful of scans means the
 * firmware is round-tripping the loop, not mid-splash. */
#define READY_SCANS 8u

/* boot_wait_ready predicate.
 * Deliberately NOT "the LCD has content": the firmware writes its boot splash
 * long before it starts polling buttons, so an LCD-content predicate returns
 * during the splash delay and every injected press is silently discarded —
 * the panel is only sampled once ScanDin runs.  Waiting for real 74HC165
 * latches is the only signal that the firmware can actually see input. */
static bool panel_scanning(nava_sim_t *ctx, void *user) {
    (void)user;
    return nava_spi_scan_count(ctx->spi) >= READY_SCANS;
}

uint64_t boot_wait_ready(nava_sim_t *ctx, uint64_t max_cycles) {
    uint64_t consumed = nava_sim_run_until(ctx, max_cycles,
                                            panel_scanning, NULL);
    if (!panel_scanning(ctx, NULL)) {
        printf("# boot_wait_ready: firmware not polling the front panel after "
               "%llu cycles (%llu scans, need %u) — boot incomplete; "
               "raise BOOT_CYCLES\n",
               (unsigned long long)consumed,
               (unsigned long long)nava_spi_scan_count(ctx->spi),
               READY_SCANS);
    } else {
        const char *line = nava_lcd_get_line(ctx->lcd, 0);
        printf("# boot_wait_ready: ready after %llu cycles "
               "(%llu panel scans); LCD[0]=\"%.16s\"\n",
               (unsigned long long)consumed,
               (unsigned long long)nava_spi_scan_count(ctx->spi),
               line ? line : "");
    }
    return consumed;
}

/* Anything arriving within this window of the previous trigger word is the
 * Timer2 restore write for that same step, not a new step.
 *
 * This is a classification threshold, not an assertion tolerance, so it wants to sit
 * in the middle of a wide empty bracket rather than just clear of one edge.  It was
 * NAVA_TRIG_OFF_CYCLES + CYCLE_TOLERANCE*16 = 32512, only ~500 cycles above the thing
 * it excludes: the restore is observed at the TRIG_CS deassert that ends a 2-byte SPI
 * burst, so its measured delta carries the Timer1 ISR's own length, and adding work to
 * CountPPQN() pushed it to 32614 - past the threshold, where it was counted as a step
 * onset and silently shifted every index after it.  That failure mode is invisible
 * except as a nonsensical inter-onset delta.
 *
 * The bracket: above the restore interval (32000 plus ISR latency, order 1k cycles),
 * below both the flam interval (320000, which the flam test must still see as an
 * onset) and the shortest step period the suite runs (2000064 at 120 BPM / 1-16;
 * 480000 at the firmware's fastest legal 250 BPM / 1-32).  4x the restore interval
 * sits an order of magnitude clear on both sides. */
#define ONSET_GAP_MIN (NAVA_TRIG_OFF_CYCLES * 4ULL)

const sim_event_t *event_log_find_step_onset(const event_log_t *log,
                                              uint64_t from, int n) {
    const sim_event_t *last_onset = NULL;
    int found = 0;
    for (size_t i = 0; i < log->count; i++) {
        const sim_event_t *e = &log->buf[i];
        if (e->type != EVT_TRIG_WORD || e->cycle < from) continue;
        if (last_onset && e->cycle - last_onset->cycle < ONSET_GAP_MIN)
            continue;                      /* Timer2 restore for the same step */
        last_onset = e;
        if (found++ == n) return e;
    }
    return NULL;
}

int test_run_all(const char *elf_path) {
    printf("TAP version 13\n");
    printf("1..%d\n", g_n_tests);
    printf("# ELF: %s\n", elf_path);

    int total_failures = 0;

    for (int t = 0; t < g_n_tests; t++) {
        const test_entry_t *entry = &g_tests[t];
        g_test_ok    = 1;
        g_test_fails = 0;

        /* Create a fresh sim context for each test */
        nava_sim_t *ctx = nava_sim_create(elf_path);
        if (!ctx) {
            tap_not_ok(t + 1, entry->name);
            tap_diag("  Could not load ELF: %s", elf_path);
            total_failures++;
            continue;
        }

        /* Build and seed EEPROM image */
        uint8_t *img = NULL;
        if (entry->eeprom_image) {
            /* Caller-supplied raw image */
            nava_sim_seed_eeprom(ctx, entry->eeprom_image, entry->eeprom_size);
        } else {
            /* Generate from fixture settings */
            img = fx_make_eeprom_image(entry->pattern0,
                                        entry->ext_channel,
                                        entry->ptrn_change_sync);
            nava_sim_seed_eeprom(ctx, img, FX_EEPROM_SIZE);
        }

        /* Run the test function */
        entry->fn(ctx);

        /* Evaluate result */
        if (g_test_ok && g_test_fails == 0) {
            tap_ok(t + 1, entry->name);
        } else {
            tap_not_ok(t + 1, entry->name);
            total_failures++;
        }

        free(img);
        nava_sim_destroy(ctx);
    }

    printf("# Total failures: %d / %d\n", total_failures, g_n_tests);
    return total_failures;
}

/* Each test binary supplies its own main() that calls test_register() then
 * test_run_all().  The framework does not supply main() to avoid duplicate
 * symbol errors when multiple test .c files are compiled as separate binaries. */
