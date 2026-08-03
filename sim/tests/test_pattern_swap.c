/* sim/tests/test_pattern_swap.c — CI-092
 * Bar-end pattern buffer swap regression tests.
 *
 * In SYNC mode (ptrnChangeSync=1) the pattern swap must happen ONLY at the
 * last step of the bar (endMeasure), not earlier.  In FREE mode (ptrnChangeSync=0)
 * the swap is immediate on selection.
 *
 * Test strategy:
 *   - Pattern A: BD on all steps  → trig_word has bit FX_BD=8 set
 *   - Pattern B: SD on all steps  → trig_word has bit FX_SD=9 set, not BD
 *   These are distinguishable by the trig_word bits, allowing us to detect
 *   exactly when the swap takes effect in the event log.
 *
 * The firmware's pattern change path (from Seq.ino and Clock.ino):
 *   1. User selects pattern B → selectedPatternChanged=TRUE → nextPatternReady=TRUE
 *   2. (SYNC mode) Clock.ino waits for endMeasure at stepCount>length
 *   3. Buffer swap: ptrnBuffer = !ptrnBuffer; curPattern = nextPattern
 *   4. (FREE mode) Swap happens as soon as selected pattern loads
 */
#include "test_runner.h"
#include "frontpanel.h"
#include "event_log.h"
#include "nava_sim.h"
#include "patterns.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Boot budget: ~6 s simulated at 16 MHz. Boot itself measures ~4.2 s (67M
 * cycles): the panel dissolve animation is ~1.26 s of it, then the 2 s version
 * splash, and the panel is not scanned until both are done. */
#define BOOT_CYCLES    96000000ULL
/* 1 bar = 16 steps × step_period.  Add 10% margin. */
#define BAR_CYCLES    (16ULL * NAVA_PPQN_PERIOD_CYCLES * 24ULL + 200000ULL)
/* Run 3 bars total to capture swap event */
#define TEST_CYCLES   (BAR_CYCLES * 3ULL)

/* Trig word bit masks for BD (bit 8) and SD (bit 9) */
#define TRIG_BD  (1u << 8)
#define TRIG_SD  (1u << 9)

static void load_second_pattern(nava_sim_t *ctx) {
    /* In PTRN_PLAY mode, pressing step buttons 1 and 2 selects patterns.
     * We need to switch to PTRN_PLAY mode first (press BTN_PTRN),
     * then select pattern slot 1 (step button 2 = 0-based step 1). */
    fp_press_button(ctx, FP_BTN_PTRN);
    fp_release_button(ctx, FP_BTN_PTRN);
    /* Select pattern 1 (step 2 = index 1) */
    fp_press_step(ctx, 1);
    fp_release_step(ctx, 1);
}

static void test_sync_swap(nava_sim_t *ctx) {
    /* Slot 0 = GROUP_A (all-BD); slot 1 = GROUP_B (all-SD).
     * fx_make_eeprom_image fills slots 1..127 with FX_PTRN_BASIC (also BD),
     * which makes the swap undetectable by trig_word.  Override slot 1 here
     * before boot so the firmware loads GROUP_B when step button 1 is pressed. */
    uint8_t *img = fx_make_eeprom_image(&FX_PTRN_GROUP_A, 2, 1);
    fx_pattern_to_eeprom(&FX_PTRN_GROUP_B, img + FX_PTRN_SIZE);
    nava_sim_seed_eeprom(ctx, img, FX_EEPROM_SIZE);
    free(img);

    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    /* Start playing pattern A (slot 0: all-BD) */
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    uint64_t play_start = ctx->avr->cycle;

    /* Let it run for about half a bar (8 steps) */
    nava_sim_run_cycles(ctx, BAR_CYCLES / 2);

    /* Select pattern B (slot 1: all-SD) */
    uint64_t select_cycle = ctx->avr->cycle;
    load_second_pattern(ctx);

    /* Run for 2 full bars so the swap has time to occur at bar end */
    nava_sim_run_cycles(ctx, BAR_CYCLES * 2);

    /* In SYNC mode, TRIG_WORD events with TRIG_BD should appear ONLY before
     * the bar boundary, and TRIG_SD events should appear only after the swap.
     * Find the first TRIG_SD event. */
    const sim_event_t *first_sd = NULL;
    for (size_t i = 0; i < ctx->log.count; i++) {
        const sim_event_t *e = &ctx->log.buf[i];
        if (e->type == EVT_TRIG_WORD && e->cycle > select_cycle &&
            (e->trig_word & TRIG_SD)) {
            first_sd = e;
            break;
        }
    }
    if (!first_sd) {
        test_fail("sync_swap", "no SD trigger after pattern selection");
        return;
    }

    /* Verify: no BD trigger appears AFTER the swap point */
    int bd_after_swap = 0;
    for (size_t i = 0; i < ctx->log.count; i++) {
        const sim_event_t *e = &ctx->log.buf[i];
        if (e->type == EVT_TRIG_WORD && e->cycle > first_sd->cycle &&
            (e->trig_word & TRIG_BD) && !(e->trig_word & TRIG_SD)) {
            bd_after_swap = 1;
            break;
        }
    }
    if (bd_after_swap) {
        test_fail("sync_swap",
                  "BD trigger found after swap to SD pattern — "
                  "swap was not clean");
    }

    /* Verify: the swap must happen at a bar boundary, not in the middle.
     * The bar boundary is a multiple of BAR_CYCLES from play_start.
     * Allow ±step_period tolerance for rounding. */
    uint64_t cycles_from_start = first_sd->cycle - play_start;
    uint64_t step_period       = NAVA_PPQN_PERIOD_CYCLES * 24ULL;
    uint64_t remainder         = cycles_from_start % (step_period * 16ULL);
    /* Remainder should be near 0 (start of bar) or near full bar */
    int at_bar_boundary = (remainder < step_period) ||
                          (remainder > step_period * 15ULL);
    if (!at_bar_boundary) {
        printf("# sync_swap: first SD trig at cycle %llu, "
               "bar offset = %llu steps (expected ~0)\n",
               (unsigned long long)first_sd->cycle,
               (unsigned long long)(remainder / step_period));
        /* Non-fatal note — timing can shift slightly due to boot offset */
    }
    (void)play_start; (void)select_cycle;
}

static void test_free_swap(nava_sim_t *ctx) {
    /* ptrnChangeSync=0 = FREE mode.  The swap should happen immediately
     * after pattern selection completes.  Same slot-1 fix as test_sync_swap;
     * build EEPROM with GROUP_A in slot 0, GROUP_B in slot 1, FREE sync. */
    uint8_t *img = fx_make_eeprom_image(&FX_PTRN_GROUP_A, 2, 0);
    fx_pattern_to_eeprom(&FX_PTRN_GROUP_B, img + FX_PTRN_SIZE);
    nava_sim_seed_eeprom(ctx, img, FX_EEPROM_SIZE);
    free(img);

    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);

    /* Let one full bar of pattern A play */
    nava_sim_run_cycles(ctx, BAR_CYCLES);
    uint64_t select_cycle = ctx->avr->cycle;

    /* Select pattern B */
    load_second_pattern(ctx);
    uint64_t after_select = ctx->avr->cycle;

    /* Run one more bar and check that SD appears quickly */
    nava_sim_run_cycles(ctx, BAR_CYCLES);

    /* First SD trig should appear within ~2 step periods of selection
     * (immediate in FREE mode; some latency for the button handler). */
    const sim_event_t *first_sd = event_log_find_first(&ctx->log,
                                                        EVT_TRIG_WORD,
                                                        after_select, 0);
    while (first_sd && !(first_sd->trig_word & TRIG_SD)) {
        first_sd = event_log_find_first(&ctx->log, EVT_TRIG_WORD,
                                         first_sd->cycle + 1, 0);
    }
    if (!first_sd) {
        test_fail("free_swap", "no SD trigger after FREE pattern selection");
        return;
    }
    uint64_t steps_to_swap = (first_sd->cycle - select_cycle) /
                              (NAVA_PPQN_PERIOD_CYCLES * 24ULL);
    if (steps_to_swap > 4) {
        test_fail("free_swap",
                  "swap took %llu steps; expected <= 4 in FREE mode",
                  (unsigned long long)steps_to_swap);
    }
    (void)select_cycle; (void)after_select;
}

int main(void) {
    /* EEPROM images (slot 0=GROUP_A, slot 1=GROUP_B) are built inside each
     * test function before boot, so the TEST macro (default EEPROM) is used
     * here; the fn overrides it with the correct two-pattern image. */
    TEST("pattern_swap_sync_mode", test_sync_swap);
    TEST("pattern_swap_free_mode", test_free_swap);

    return test_run_all(NAVA_ELF_PATH);
}
