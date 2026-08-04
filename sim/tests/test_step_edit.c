/* sim/tests/test_step_edit.c
 * PTRN_STEP step-LED playhead and step-button programming.
 *
 * Both of these were reported broken on hardware at 0.93 and neither was covered:
 * test_ui.c asserts the mode LEDs and the config-page step LEDs, and test_ext_inst.c
 * asserts ext MIDI, so nothing looked at the drum lane's own step LEDs or at writing
 * a step with a step button.
 *
 * Firmware facts these tests are pinned to:
 *   - curInst boots as BD (define.h), instVelLow[BD]=25, instVelHigh[BD]=50.
 *   - Led.ino PTRN_STEP running branch XORs (blinkFast << curStep) into the lane
 *     content, so with FX_PTRN_BASIC (BD on every step, velocity 50 > 25 → every bit
 *     in stepLedsHigh) the panel is all-lit with exactly ONE bit cleared: the playhead.
 *     That inversion is what makes the assertion unambiguous - the content is 0xFFFF in
 *     both flagLedIntensity branches, so any sample inside a step gives the same answer.
 *   - blinkFast is set HIGH at each step boundary and LOW half a step later
 *     (Clock.ino), so a sample must be taken in the FIRST half of a step.
 *   - Step programming in PTRN_STEP only happens while running (Seq.ino: stopped, the
 *     step buttons select a pattern instead) - that split predates this repo.
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
#define STEP_CYCLES   (NAVA_PPQN_PERIOD_CYCLES * 24ULL)   /* 2000064 */

/* Empty pattern: nothing programmed, so any trigger observed after a step press came
 * from that press. shuffle must be >= 1 (shuffle[-1] is OOB). */
static const fx_pattern_t FX_PTRN_EMPTY = {
    .length  = 15,
    .scale   = FX_SCALE_16,
    .shuffle = 1,
    .flam    = 0,
};

/* Step to program in the button test. 4 is far enough into the bar that the press
 * (which takes ~400k cycles to inject) cannot race the step it writes. */
#define PROG_STEP 4

/* Index of the single clear bit in an otherwise all-lit panel, or -1. */
static int lone_dark_bit(uint16_t leds) {
    int found = -1;
    for (int b = 0; b < 16; b++) {
        if (!(leds & (1u << b))) {
            if (found >= 0) return -1;   /* more than one dark: not a playhead */
            found = b;
        }
    }
    return found;
}

/* The playhead must walk the panel while the sequencer runs. */
static void test_ptrn_step_playhead(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);

    /* Anchor on a real step onset rather than on the press, which lands at an
     * arbitrary point inside a step. fp_press+fp_release only advance ~0.2 of a
     * step, so the onsets have to be run out before they can be looked for. */
    nava_sim_run_cycles(ctx, 3ULL * STEP_CYCLES);
    const sim_event_t *onset = event_log_find_step_onset(&ctx->log, t0, 1);
    if (!onset) {
        test_fail("stepleds/anchor", "no step onset after PLAY");
        return;
    }

    /* Sample a quarter of the way into each of 8 consecutive steps - inside the
     * blinkFast HIGH half, and clear of the boundary itself. */
    /* Samples start 3 steps past the anchor: the run above has already consumed
     * that much, and a target behind the current cycle would underflow the
     * unsigned run length. */
    int seen[8];
    for (int k = 0; k < 8; k++) {
        uint64_t target = onset->cycle + (uint64_t)(k + 3) * STEP_CYCLES + STEP_CYCLES / 4;
        nava_sim_run_cycles(ctx, target - ctx->avr->cycle);
        seen[k] = lone_dark_bit(fp_step_leds(ctx));
    }

    printf("# stepleds/playhead:");
    for (int k = 0; k < 8; k++) printf(" %d", seen[k]);
    printf("\n");

    if (seen[0] < 0) {
        test_fail("stepleds/playhead",
                  "no single-step playhead in the lane (leds=0x%04X)",
                  fp_step_leds(ctx));
        return;
    }
    for (int k = 1; k < 8; k++) {
        int expect = (seen[0] + k) % 16;
        if (seen[k] != expect) {
            test_fail("stepleds/playhead",
                      "step %d: playhead at %d, expected %d", k, seen[k], expect);
            return;
        }
    }
}

/* Index of the single lit bit, or -1 if the panel is dark or shows more than one. */
static int lone_lit_bit(uint16_t leds) {
    int found = -1;
    for (int b = 0; b < 16; b++) {
        if (leds & (1u << b)) {
            if (found >= 0) return -1;
            found = b;
        }
    }
    return found;
}

/* The same playhead, inside EXT INST edit mode, where it is driven from extCurStep.
 * FX_PTRN_BASIC has an empty ext lane, so the panel is dark and the playhead is the
 * only lit bit - the inverse of the drum-lane case above. */
static void test_ext_edit_playhead(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);

    fp_press_button(ctx, FP_BTN_SHIFT);
    fp_press_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_SHIFT);
    fp_settle(ctx);
    nava_sim_run_cycles(ctx, 16000000ULL);   /* outlast the 800 ms entry splash */

    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, 3ULL * STEP_CYCLES);

    const sim_event_t *onset = event_log_find_step_onset(&ctx->log, t0, 1);
    if (!onset) {
        test_fail("extleds/anchor", "no step onset after PLAY");
        return;
    }

    int seen[6];
    for (int k = 0; k < 6; k++) {
        uint64_t target = onset->cycle + (uint64_t)(k + 3) * STEP_CYCLES + STEP_CYCLES / 4;
        nava_sim_run_cycles(ctx, target - ctx->avr->cycle);
        seen[k] = lone_lit_bit(fp_step_leds(ctx));
    }

    printf("# extleds/playhead:");
    for (int k = 0; k < 6; k++) printf(" %d", seen[k]);
    printf("\n");

    if (seen[0] < 0) {
        test_fail("extleds/playhead",
                  "no single-step playhead on the ext lane (leds=0x%04X)",
                  fp_step_leds(ctx));
        return;
    }
    for (int k = 1; k < 6; k++) {
        int expect = (seen[0] + k) % 16;
        if (seen[k] != expect) {
            test_fail("extleds/playhead",
                      "step %d: playhead at %d, expected %d", k, seen[k], expect);
            return;
        }
    }
}

/* A step button must write the selected instrument's step while running. */
static void test_ptrn_step_button_programs(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);

    /* Program the step, then listen over a full bar for the trigger it should make.
     * The pattern is empty, so ANY trigger word in that window is this step's. */
    fp_press_step(ctx, PROG_STEP);
    fp_release_step(ctx, PROG_STEP);
    fp_settle(ctx);

    uint64_t t0 = ctx->avr->cycle;
    event_log_clear(&ctx->log);
    nava_sim_run_cycles(ctx, 17ULL * STEP_CYCLES);

    const sim_event_t *fired = event_log_find_step_onset(&ctx->log, t0, 0);
    if (!fired) {
        test_fail("stepedit/program",
                  "no trigger in a bar after programming step %d", PROG_STEP);
        return;
    }
    printf("# stepedit/program: trig word 0x%04X\n", (unsigned)fired->trig_word);
    /* BD is bit 8 of the trigger word (define.h). */
    if (!(fired->trig_word & (1u << 8))) {
        test_fail("stepedit/program",
                  "trigger word 0x%04X has no BD bit", (unsigned)fired->trig_word);
    }
}

/* What a unit whose stored sync is SLAVE does with no incoming MIDI clock, which is the
 * reported symptom triad: PLAY latches, nothing sounds, no step LED moves, and step
 * programming still works because it is gated on isRunning alone.
 *
 * SetSeqSync() (SeqConf.ino) calls TimerStop() for SLAVE, so CountPPQN() is driven only
 * by incoming MIDI clock; the sim sends none. seq.sync is byte 0 of the setup record and
 * survives a reflash, which is why the symptom follows the machine across firmware
 * versions rather than the firmware.
 *
 * This test documents the failure mode rather than a defect: SLAVE with no clock is
 * SUPPOSED to sit still. It exists so the triad is attributable next time. */
static void test_slave_no_clock_is_inert(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, 4ULL * STEP_CYCLES);

    /* FX_PTRN_BASIC has BD on every step: as MASTER this window is 4 triggers. */
    if (event_log_find_step_onset(&ctx->log, t0, 0)) {
        test_fail("slave/inert", "SLAVE with no clock still triggered a step");
    }

    /* The panel is frozen: whatever it shows, it shows the same thing a step later.
     * (It is not blank - blinkFast powers up HIGH and nothing clears it without a
     * clock, so step 0 stays cut out of the lane. It just never moves.) */
    uint16_t leds_a = fp_step_leds(ctx);
    nava_sim_run_cycles(ctx, STEP_CYCLES);
    uint16_t leds_b = fp_step_leds(ctx);
    printf("# slave/inert: step leds 0x%04X -> 0x%04X (no clock in)\n", leds_a, leds_b);
    if (leds_a != leds_b) {
        test_fail("slave/inert", "step LEDs moved without a clock: 0x%04X -> 0x%04X",
                  leds_a, leds_b);
    }

    /* ...and a step button still edits, because that is gated on isRunning only. */
    fp_press_step(ctx, PROG_STEP);
    fp_release_step(ctx, PROG_STEP);
    fp_settle(ctx);
}

/* A stored pattern whose setup bytes are zero must still play.
 *
 * scale is the divisor of `ppqn % scale` in CountPPQN() and shuffle is used as
 * `shuffle[pattern.shuffle - 1]`, so a zero in either byte does not degrade playback -
 * it stops it, while leaving isRunning TRUE. The machine then latches PLAY, sounds
 * nothing and moves no step LED, but still accepts step programming, which is precisely
 * the symptom this was reported as. Zeros get there by more than one route: a paste from
 * the copy buffer before anything was copied into it (bufferedPattern is BSS, and MUTE in
 * TRACK_WRITE pastes), a restore of a record made elsewhere, or a partially written part.
 * The firmware must be able to play any of them. */
static void test_zeroed_setup_bytes_still_play(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, 4ULL * STEP_CYCLES);

    /* BD is on every step of the fixture, so a working machine fires here. */
    if (!event_log_find_step_onset(&ctx->log, t0, 0)) {
        test_fail("zeroed/play",
                  "a pattern with zeroed length/scale/shuffle triggered nothing");
        return;
    }

    /* And the panel has to move with it. */
    uint16_t leds_a = fp_step_leds(ctx);
    nava_sim_run_cycles(ctx, STEP_CYCLES / 2);
    uint16_t leds_b = fp_step_leds(ctx);
    printf("# zeroed/play: step leds 0x%04X -> 0x%04X\n", leds_a, leds_b);
    if (leds_a == leds_b) {
        test_fail("zeroed/play", "step LEDs frozen at 0x%04X", leds_a);
    }
}

int main(void) {
    TEST_WITH_PATTERN("stepleds_playhead_chases_in_ptrn_step",
                      test_ptrn_step_playhead, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("stepleds_playhead_chases_in_ext_edit",
                      test_ext_edit_playhead, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("stepedit_step_button_programs_inst",
                      test_ptrn_step_button_programs, &FX_PTRN_EMPTY, 2, 1);

    /* Same fixture as the first test, with the setup record's sync byte set to SLAVE. */
    uint8_t *slave_img = fx_make_eeprom_image(&FX_PTRN_BASIC, 2, 1);
    slave_img[FX_OFFSET_SETUP] = 1;   /* 0=MASTER, 1=SLAVE, 2=EXPANDER */
    static test_entry_t slave_entry = {
        .name = "slave_sync_without_clock_is_inert",
        .fn = test_slave_no_clock_is_inert,
        .ext_channel = 2, .ptrn_change_sync = 1,
    };
    slave_entry.eeprom_image = slave_img;
    slave_entry.eeprom_size = FX_EEPROM_SIZE;
    test_register(&slave_entry);

    /* Pattern 0's four setup bytes zeroed in place, which is what a paste from an
     * untouched copy buffer writes: 32 bytes of trigger words, then length, scale,
     * shuffle, flam. The trigger words are left alone so the pattern still has content
     * to play. */
    uint8_t *zeroed_img = fx_make_eeprom_image(&FX_PTRN_BASIC, 2, 1);
    for (int b = 32; b < 36; b++) zeroed_img[b] = 0;
    static test_entry_t zeroed_entry = {
        .name = "zeroed_pattern_setup_bytes_still_play",
        .fn = test_zeroed_setup_bytes_still_play,
        .ext_channel = 2, .ptrn_change_sync = 1,
    };
    zeroed_entry.eeprom_image = zeroed_img;
    zeroed_entry.eeprom_size = FX_EEPROM_SIZE;
    test_register(&zeroed_entry);

    int rc = test_run_all(NAVA_ELF_PATH);
    free(slave_img);
    free(zeroed_img);
    return rc;
}
