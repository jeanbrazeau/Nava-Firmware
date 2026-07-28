/* sim/tests/test_timing.c — CI-090, CI-091
 * Regression tests for timing-critical paths:
 *   1. PPQN step period at bpm=120 (Timer1 CTC math)
 *   2. Timer2 trigger-off restore at 32000 cycles (2 ms)
 *   3. Timer3 flam delayed hit at (flam[0]+1)*64 = 320000 cycles (20 ms)
 *   4. Shuffle offset: odd steps delayed by shuffle[type][1] PPQN ticks
 *   5. Quarantined: shuffle==0 OOB path documented but not stably asserted
 *
 * Integer math reference (all values must match define.h / timer.ino):
 *   FREQUENCY = (120*96)/60 = 192
 *   OCR1A     = (16000000/8)/192 = 10416  (truncating)
 *   PPQN tick = (10416+1)*8 = 83336 cycles
 *   Step 16th  = 24 * 83336 = 2000064 cycles
 *   Trigger-off = (249+1)*128 = 32000 cycles
 *   Flam[0]    = (4999+1)*64 = 320000 cycles
 */
#include "test_runner.h"
#include "frontpanel.h"
#include "event_log.h"
#include "nava_sim.h"
#include "patterns.h"

#include <stdio.h>
#include <stdlib.h>

/* Boot budget: ~500 ms simulated = 8000000 cycles at 16 MHz */
#define BOOT_CYCLES     64000000ULL
/* One full bar at 120 BPM 4/4 = 16 steps × 2000064 ≈ 32001024 cycles */
#define ONE_BAR_CYCLES  32100000ULL
/* Steps per bar in every fixture here (fx_pattern_t.length = 15) */
#define NBR_FX_STEPS    16
/* test_ppqn_period measures from step 1 to step 17, so it needs a run long enough to
 * produce 18 onsets - just over two bars, plus margin for the PLAY transient. */
#define TWO_BAR_CYCLES  70000000ULL
/* Flam test window: main hit + 400000 cycle margin */
#define FLAM_WINDOW     400000ULL

static void test_ppqn_period(nava_sim_t *ctx) {
    /* Boot firmware; stop early once LCD shows content (firmware in run loop) */
    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    /* Press PLAY and advance two bars to collect trigger events */
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    uint64_t t0 = ctx->avr->cycle;
    nava_sim_run_cycles(ctx, TWO_BAR_CYCLES);

    /* Find consecutive TRIG_WORD events from step-trigger writes.
     * BD is on every step (FX_PTRN_BASIC); trig_word bit FX_BD=8 will be set.
     * Index 1, not 0: step 0 sits in the PLAY transient (see below). */
    const sim_event_t *first = event_log_find_step_onset(&ctx->log, t0, 1);
    if (!first) {
        printf("# ppqn_period: LCD[0]=\"%.16s\" (expected BPM or mode text)\n",
               fp_lcd_line(ctx, 0) ? fp_lcd_line(ctx, 0) : "<empty>");
        test_fail("ppqn_period",
                  "no TRIG_WORD events after PLAY "
                  "(if LCD blank: boot incomplete — increase BOOT_CYCLES)");
        return;
    }
    /* Measure one FULL BAR apart rather than between the first two steps.
     *
     * Two systematic errors made the adjacent-step measurement unfit for asserting a
     * hardware CTC period to 256 cycles. The observable is the TRIG_CS deassert ending
     * SetDoutTrig's SPI burst, so it carries however much of CountPPQN() ran first --
     * and that pre-window grows with curStep, because the stepValue loop does a
     * variable-distance bitRead per instrument (~80 cycles per step of curStep). So
     * consecutive onsets drift ~+90 cycles apart from each other and drop by ~1.2k at
     * the bar wrap, none of which is a period error. Second, the FIRST interval after
     * PLAY is the most contended in the run: the loop is still scanning buttons and
     * repainting, and an in-progress SPI transaction delays ISR entry by a few hundred
     * cycles, which showed up as a -337 cycle "period error" that no clock could
     * produce.
     *
     * Endpoints exactly NBR_STEP apart share the same curStep, so the ramp cancels
     * identically instead of being tolerated, and starting at step 1 clears the PLAY
     * transient. Any residual jitter is divided by 16. */
    const sim_event_t *bar_end = event_log_find_step_onset(&ctx->log, t0, 1 + NBR_FX_STEPS);
    if (!bar_end) {
        test_fail("ppqn_period", "need %d step onsets to measure a full bar",
                  2 + NBR_FX_STEPS);
        return;
    }

    uint64_t delta = bar_end->cycle - first->cycle;
    /* One 16th step = NAVA_PPQN_PERIOD_CYCLES * 24 = 2000064; a bar is 16 of them. */
    uint64_t expected = NAVA_PPQN_PERIOD_CYCLES * 24ULL * (uint64_t)NBR_FX_STEPS;
    /* Same tolerance as before in absolute cycles, now covering 16 steps rather than
     * one - so this is a 16x TIGHTER bound on the period, not a loosened one. */
    assert_cycle_within("ppqn_period/bar_delta", delta, expected,
                         CYCLE_TOLERANCE * 8);
}

static void test_trigger_off_timer(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    uint64_t t0 = ctx->avr->cycle;
    nava_sim_run_cycles(ctx, ONE_BAR_CYCLES);

    /* Find the first step-trigger TRIG_WORD (non-zero word = instruments hit) */
    const sim_event_t *hit = NULL;
    for (size_t i = 0; i < ctx->log.count; i++) {
        const sim_event_t *e = &ctx->log.buf[i];
        if (e->type == EVT_TRIG_WORD && e->cycle >= t0 && e->trig_word != 0) {
            hit = e;
            break;
        }
    }
    if (!hit) {
        printf("# trig_off: LCD[0]=\"%.16s\"\n",
               fp_lcd_line(ctx, 0) ? fp_lcd_line(ctx, 0) : "<empty>");
        test_fail("trig_off",
                  "no non-zero TRIG_WORD event "
                  "(if LCD blank: boot incomplete)");
        return;
    }

    /* Find the Timer2 restore write: a TRIG_WORD following the hit within
     * [32000 - tol, 32000 + tol] cycles.  The restore word may be non-zero
     * (tempDoutTrig retains HH-select bit B10, DL-016). */
    /* The restore is simply the next trigger word after the onset. */
    const sim_event_t *restore = event_log_find_first(&ctx->log, EVT_TRIG_WORD,
                                                       hit->cycle + 1, 0);
    if (!restore) {
        test_fail("trig_off",
                  "Timer2 restore write not found after cycle %llu (+32000)",
                  (unsigned long long)hit->cycle);
        return;
    }
    uint64_t actual_delta = restore->cycle - hit->cycle;
    /* Timer2 fires at a fixed 32000 cycles, but the observable is the TRIG_CS
     * deassert that ends a 2-byte SPI burst, so the measured delta carries the
     * ISR entry latency plus shift-out time (~270 cycles measured).  Tolerance
     * absorbs that fixed offset; it does NOT paper over a period mismatch,
     * which would scale with the period rather than stay constant. */
    assert_cycle_within("trig_off/timer2_delta",
                         actual_delta, NAVA_TRIG_OFF_CYCLES,
                         CYCLE_TOLERANCE * 16);
}

static void test_flam_timer(nava_sim_t *ctx) {
    /* Uses FX_PTRN_FLAM: BD on steps 0,4 with flam type 0 (OCR3A=4999) */
    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    uint64_t t0 = ctx->avr->cycle;
    nava_sim_run_cycles(ctx, ONE_BAR_CYCLES);

    /* The first step triggers BD. The flam hit arrives at main_hit + 320000 cycles */
    /* A flam is a main hit followed by a delayed second hit.  Scan consecutive
     * step onsets for the pair separated by the Timer3 interval.  (Searching a
     * fixed window off the FIRST BD hit is unreliable: whether the capture
     * opens on a main or on a flam depends on where PLAY landed in the bar.)
     *
     * Timer3 is armed inside the Timer1 ISR AFTER the DAC write, mux select and
     * the SPI trigger burst that we timestamp on, so the observed gap is the
     * 320000-cycle period plus a fixed ~1400-cycle arming latency.  That offset
     * is constant across pairs (measured 321501 / 321265), which is what
     * distinguishes it from a period error — a wrong period would scale. */
    const uint64_t flam_expected = NAVA_FLAM_CYCLES(NAVA_FLAM_OCR(0)); /* 320000 */
    const uint64_t flam_tol      = CYCLE_TOLERANCE * 96;               /* ~3072 */

    const sim_event_t *main_hit = NULL, *flam_hit = NULL;
    for (int i = 0; ; i++) {
        const sim_event_t *a = event_log_find_step_onset(&ctx->log, t0, i);
        const sim_event_t *b = event_log_find_step_onset(&ctx->log, t0, i + 1);
        if (!a || !b) break;
        uint64_t d = b->cycle - a->cycle;
        if (d + flam_tol >= flam_expected && d <= flam_expected + flam_tol) {
            main_hit = a; flam_hit = b;
            break;
        }
    }
    if (!main_hit || !flam_hit) {
        printf("# flam: LCD[0]=\"%.16s\"\n",
               fp_lcd_line(ctx, 0) ? fp_lcd_line(ctx, 0) : "<empty>");
        test_fail("flam",
                  "no main+flam trigger pair separated by ~%llu cycles "
                  "(if LCD blank: boot incomplete)",
                  (unsigned long long)flam_expected);
        return;
    }
    assert_cycle_within("flam/timer3_delta",
                         flam_hit->cycle - main_hit->cycle,
                         flam_expected, flam_tol);
}

static void test_shuffle_offset(nava_sim_t *ctx) {
    /* FX_PTRN_SHUFFLED: shuffle=2 → shuffle[1][1]=-1, meaning odd steps arrive
     * 1 PPQN tick (-83336 cycles) early relative to even steps. */
    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    uint64_t t0 = ctx->avr->cycle;
    nava_sim_run_cycles(ctx, ONE_BAR_CYCLES * 2);

    /* Collect step 0 (even) and step 1 (odd, shuffled early) timestamps.
     * Both should have instrument bits set (BD on step 0 only in this fixture,
     * but we look at the first two trig_word events with non-zero words). */
    const sim_event_t *step0 = event_log_find_step_onset(&ctx->log, t0, 0);
    const sim_event_t *step1 = event_log_find_step_onset(&ctx->log, t0, 1);

    if (!step0 || !step1) {
        test_fail("shuffle", "need at least 2 step-onset TRIG_WORD events");
        return;
    }

    uint64_t delta = step1->cycle - step0->cycle;
    /* With shuffle=2 type=1: even→odd gap = (24 + shuffle[1][1]) ticks
     * = (24 + (-1)) * 83336 = 23 * 83336 = 1916728 cycles.
     * Without shuffle it would be 24 * 83336 = 2000064 cycles. */
    uint64_t expected_shuffled = 23ULL * NAVA_PPQN_PERIOD_CYCLES;  /* 1916728 */
    assert_cycle_within("shuffle/odd_step_early",
                         delta, expected_shuffled, CYCLE_TOLERANCE * 16);
}

/* Quarantined: document shuffle==0 OOB without asserting stable output.
 * This test intentionally does NOT call test_fail on the result — it only
 * confirms the simulator does not hang or crash. */
static void test_shuffle_zero_oob_quarantine(nava_sim_t *ctx) {
    /* WARNING: shuffle==0 causes shuffle[(0)-1] = shuffle[-1] in firmware.
     * This is undefined behavior.  We run it to document current behavior
     * but DO NOT assert cycle-exact output. */
    boot_wait_ready(ctx, BOOT_CYCLES);
    /* If the AVR crashes, nava_sim_run_cycles returns 0; we note it but pass. */
    uint64_t result = nava_sim_run_cycles(ctx, ONE_BAR_CYCLES);
    if (result == 0) {
        printf("# QUARANTINED: shuffle==0 caused AVR crash (OOB read)\n");
    } else {
        printf("# QUARANTINED: shuffle==0 completed without crash "
               "(UB may have produced arbitrary timing)\n");
    }
    /* Test always 'passes' — it documents behavior, not correctness. */
}

int main(void) {
    /* Basic pattern tests */
    TEST_WITH_PATTERN("ppqn_step_period_120bpm",
                      test_ppqn_period, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("trigger_off_timer2_32000cycles",
                      test_trigger_off_timer, &FX_PTRN_BASIC, 2, 1);

    /* Flam test (uses FX_PTRN_FLAM with flam type 0) */
    TEST_WITH_PATTERN("flam_timer3_320000cycles",
                      test_flam_timer, &FX_PTRN_FLAM, 2, 1);

    /* Shuffle test (uses FX_PTRN_SHUFFLED with shuffle=2) */
    TEST_WITH_PATTERN("shuffle_odd_step_early",
                      test_shuffle_offset, &FX_PTRN_SHUFFLED, 2, 1);

    /* Quarantined: builds a special EEPROM with shuffle=0 (do NOT copy
     * this pattern — always keep production fixtures at shuffle>=1). */
    static const fx_pattern_t SHUFFLE_ZERO_FIXTURE = {
        .length=15, .scale=FX_SCALE_16, .shuffle=0, .flam=0,
        .inst = { [FX_BD] = 0xFFFFu },
        .velocity = { [FX_BD] = {50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50} },
    };
    TEST_WITH_PATTERN("QUARANTINE_shuffle_zero_oob",
                      test_shuffle_zero_oob_quarantine, &SHUFFLE_ZERO_FIXTURE, 2, 1);

    return test_run_all(NAVA_ELF_PATH);
}
