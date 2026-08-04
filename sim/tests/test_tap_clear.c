/* sim/tests/test_tap_clear.c
 * PTRN_TAP: holding CLEAR + an instrument button must erase that instrument's steps.
 *
 * Reported broken on hardware: in TAP mode the erase does nothing. Nothing covered it -
 * test_step_edit.c only drives PTRN_STEP.
 *
 * Firmware facts these tests are pinned to:
 *   - SHIFT+TAP selects PTRN_TAP (Seq.ino).
 *   - FX_PTRN_BASIC has BD on all 16 steps, so a working machine fires a trigger word
 *     with bit 8 (BD) set on every step, and an erased BD lane fires nothing at all.
 *   - The CLEAR handler erases pattern[ptrnBuffer].inst[] at curStep and mutes the
 *     instrument while held, so triggers stop during the hold whether or not the erase
 *     lands - the assertion window has to be AFTER the release.
 *   - PTRN_TAP commits edits from bufferedPattern, not from pattern[ptrnBuffer]
 *     (Seq.ino patternWasEdited branch), so the second test - reselect the pattern,
 *     which reloads it out of patternBank - is what shows whether the erase was
 *     committed or only lived in the play buffer.
 */
#include "test_runner.h"
#include "frontpanel.h"
#include "event_log.h"
#include "nava_sim.h"
#include "patterns.h"

#include <stdio.h>
#include <stdlib.h>

/* Boot budget: ~6 s simulated. Boot measures ~4.2 s (dissolve + splash). */
#define BOOT_CYCLES   96000000ULL
#define STEP_CYCLES   (NAVA_PPQN_PERIOD_CYCLES * 24ULL)
#define BAR_CYCLES    (16ULL * STEP_CYCLES)

#define BD_TRIG_BIT   (1u << 8)
#define SD_TRIG_BIT   (1u << 9)

/* BD and SD on every step. Erasing BD must leave SD alone, which is what tells an
 * instrument-scoped erase apart from a whole-pattern clobber - both look identical
 * if the fixture only carries the instrument being erased. */
/* Nothing programmed, so any trigger observed is the one the test just entered.
 * shuffle must be >= 1 (shuffle[-1] is OOB). */
static const fx_pattern_t FX_PTRN_EMPTY = {
    .length  = 15,
    .scale   = FX_SCALE_16,
    .shuffle = 1,
    .flam    = 0,
};

static const fx_pattern_t FX_PTRN_BD_SD = {
    .length  = 15,
    .scale   = FX_SCALE_16,
    .shuffle = 1,
    .flam    = 0,
    .inst    = { [FX_BD] = 0xFFFFu, [FX_SD] = 0xFFFFu },
    .velocity = {
        [FX_BD] = {50,50,50,50, 50,50,50,50, 50,50,50,50, 50,50,50,50},
        [FX_SD] = {50,50,50,50, 50,50,50,50, 50,50,50,50, 50,50,50,50},
    },
};

/* Enter PTRN_TAP and start the sequencer.
 *
 * The mode has to be proven, not assumed: PTRN_STEP's own CLEAR branch erases
 * inst[curInst], and curInst boots as BD, so a test that silently stayed in PTRN_STEP
 * would pass on the wrong code path. Led.ino separates them unambiguously -
 * ptrnLed blinks only in PTRN_STEP, tapLed only in PTRN_TAP, and neither is lit by the
 * other mode. Both blink on blinkTempo, so the panel is sampled across a bar and the
 * samples OR'd. Returns false on a failed mode check, having already reported it. */
static bool tap_mode_running(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);

    fp_press_button(ctx, FP_BTN_SHIFT);
    fp_press_button(ctx, FP_BTN_TAP);
    fp_release_button(ctx, FP_BTN_TAP);
    fp_release_button(ctx, FP_BTN_SHIFT);
    fp_settle(ctx);

    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, STEP_CYCLES);

    uint16_t seen = 0;
    for (int k = 0; k < 16; k++) {
        nava_sim_run_cycles(ctx, STEP_CYCLES);
        seen |= fp_config_leds(ctx);
    }
    printf("# tapclear/mode: config LEDs over a bar 0x%04X (ptrn=%d tap=%d)\n",
           seen, (seen >> 9) & 1, (seen >> 10) & 1);
    if (!((seen >> 10) & 1) || ((seen >> 9) & 1)) {
        test_fail("tapclear/mode",
                  "SHIFT+TAP did not select PTRN_TAP (config LEDs 0x%04X)", seen);
        return false;
    }
    return true;
}

/* Hold CLEAR + the BD button across two bars, which covers every step at least once. */
static void hold_clear_bd(nava_sim_t *ctx) {
    fp_press_button(ctx, FP_BTN_CLEAR);
    fp_press_step(ctx, 0);              /* BD_BTN = step button 1 */
    nava_sim_run_cycles(ctx, 2ULL * BAR_CYCLES);
    fp_release_step(ctx, 0);
    fp_release_button(ctx, FP_BTN_CLEAR);
    fp_settle(ctx);
}

/* Count trigger words carrying the given instrument bit over the next bar. */
static int triggers_in_a_bar(nava_sim_t *ctx, uint16_t inst_bit) {
    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;
    nava_sim_run_cycles(ctx, BAR_CYCLES + STEP_CYCLES);

    int n = 0;
    uint64_t from = t0;
    for (;;) {
        const sim_event_t *e = event_log_find_step_onset(&ctx->log, from, 0);
        if (!e) break;
        if (e->trig_word & inst_bit) n++;
        from = e->cycle + 1;
    }
    return n;
}

/* Both counts over one bar, so the two lanes are read from the same window. */
static void triggers_bd_sd(nava_sim_t *ctx, int *bd, int *sd) {
    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;
    nava_sim_run_cycles(ctx, BAR_CYCLES + STEP_CYCLES);

    *bd = 0; *sd = 0;
    uint64_t from = t0;
    for (;;) {
        const sim_event_t *e = event_log_find_step_onset(&ctx->log, from, 0);
        if (!e) break;
        if (e->trig_word & BD_TRIG_BIT) (*bd)++;
        if (e->trig_word & SD_TRIG_BIT) (*sd)++;
        from = e->cycle + 1;
    }
}

/* The erase must be audible: after the hold, the BD lane is silent. */
static void test_tap_clear_erases_lane(nava_sim_t *ctx) {
    if (!tap_mode_running(ctx)) return;

    int before = triggers_in_a_bar(ctx, BD_TRIG_BIT);
    printf("# tapclear/erase: %d BD triggers before CLEAR\n", before);
    if (before == 0) {
        test_fail("tapclear/erase", "fixture never triggered BD - nothing to erase");
        return;
    }

    hold_clear_bd(ctx);

    int after = triggers_in_a_bar(ctx, BD_TRIG_BIT);
    printf("# tapclear/erase: %d BD triggers after CLEAR+BD held for two bars\n", after);
    if (after != 0) {
        test_fail("tapclear/erase",
                  "BD still fires %d times a bar after CLEAR+BD (was %d)", after, before);
    }
}

/* The erase is scoped to the instrument, and it is what gets committed.
 *
 * PTRN_TAP commits pattern edits from bufferedPattern rather than from
 * pattern[ptrnBuffer] (Seq.ino, the patternWasEdited branch), so an erase that only
 * writes the play buffer commits whatever bufferedPattern happens to hold. A fixture
 * carrying only the erased instrument cannot see this - a stale or empty commit reads
 * the same as a successful erase. SD is here to tell them apart, and the reselect is
 * what forces the committed bank copy back into the play buffer. */
static void test_tap_clear_is_scoped_and_committed(nava_sim_t *ctx) {
    if (!tap_mode_running(ctx)) return;

    int bd, sd;
    triggers_bd_sd(ctx, &bd, &sd);
    printf("# tapclear/commit: before CLEAR: BD=%d SD=%d\n", bd, sd);
    if (bd == 0 || sd == 0) {
        test_fail("tapclear/commit", "fixture did not play both lanes (BD=%d SD=%d)", bd, sd);
        return;
    }

    hold_clear_bd(ctx);

    triggers_bd_sd(ctx, &bd, &sd);
    printf("# tapclear/commit: after CLEAR+BD: BD=%d SD=%d\n", bd, sd);
    if (sd == 0) {
        test_fail("tapclear/commit", "CLEAR+BD also silenced SD in the play buffer");
    }

    /* Leave to pattern 2 and come back to pattern 1, both loaded out of the bank.
     * PTRN_PLAY is where step buttons select a pattern. */
    fp_press_button(ctx, FP_BTN_PTRN);
    fp_release_button(ctx, FP_BTN_PTRN);
    fp_settle(ctx);

    fp_press_step(ctx, 1);              /* pattern 2 */
    fp_release_step(ctx, 1);
    nava_sim_run_cycles(ctx, 2ULL * BAR_CYCLES);

    fp_press_step(ctx, 0);              /* back to pattern 1 */
    fp_release_step(ctx, 0);
    nava_sim_run_cycles(ctx, 2ULL * BAR_CYCLES);

    triggers_bd_sd(ctx, &bd, &sd);
    printf("# tapclear/commit: after reselecting the pattern: BD=%d SD=%d\n", bd, sd);
    if (bd != 0) {
        test_fail("tapclear/commit",
                  "the erased BD lane came back (%d triggers a bar) when the pattern "
                  "was reloaded from patternBank", bd);
    }
    if (sd == 0) {
        test_fail("tapclear/commit",
                  "SD was lost by the reload: CLEAR committed a stale bufferedPattern "
                  "over the whole pattern instead of the erase");
    }
}

/* The other half of the same commit path: tapping must still record and still survive
 * the reload. This is what a change to the PTRN_TAP commit source would break. */
static void test_tap_records_and_commits(nava_sim_t *ctx) {
    if (!tap_mode_running(ctx)) return;

    /* Empty fixture, so any BD trigger from here is the tap's. */
    if (triggers_in_a_bar(ctx, BD_TRIG_BIT) != 0) {
        test_fail("taprecord/commit", "empty fixture triggered BD before any tap");
        return;
    }

    /* Four taps spread across a bar, each landing on a different step. */
    for (int k = 0; k < 4; k++) {
        fp_press_step(ctx, 0);          /* BD_BTN */
        fp_release_step(ctx, 0);
        nava_sim_run_cycles(ctx, 3ULL * STEP_CYCLES);
    }

    int recorded = triggers_in_a_bar(ctx, BD_TRIG_BIT);
    printf("# taprecord/commit: %d BD triggers a bar after 4 taps\n", recorded);
    if (recorded == 0) {
        test_fail("taprecord/commit", "tapping BD recorded nothing");
        return;
    }

    fp_press_button(ctx, FP_BTN_PTRN);
    fp_release_button(ctx, FP_BTN_PTRN);
    fp_settle(ctx);
    fp_press_step(ctx, 1);              /* pattern 2 */
    fp_release_step(ctx, 1);
    nava_sim_run_cycles(ctx, 2ULL * BAR_CYCLES);
    fp_press_step(ctx, 0);              /* back to pattern 1 */
    fp_release_step(ctx, 0);
    nava_sim_run_cycles(ctx, 2ULL * BAR_CYCLES);

    int after = triggers_in_a_bar(ctx, BD_TRIG_BIT);
    printf("# taprecord/commit: %d BD triggers after reselecting the pattern\n", after);
    if (after != recorded) {
        test_fail("taprecord/commit",
                  "tapped steps did not survive the reload: %d recorded, %d after",
                  recorded, after);
    }
}

int main(void) {
    TEST_WITH_PATTERN("tap_clear_erases_instrument_lane",
                      test_tap_clear_erases_lane, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("tap_clear_scoped_and_survives_reload",
                      test_tap_clear_is_scoped_and_committed, &FX_PTRN_BD_SD, 2, 1);
    TEST_WITH_PATTERN("tap_recording_still_commits",
                      test_tap_records_and_commits, &FX_PTRN_EMPTY, 2, 1);
    return test_run_all(NAVA_ELF_PATH);
}
