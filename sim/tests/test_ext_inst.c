/* sim/tests/test_ext_inst.c — CI-093
 * EXT_INST MIDI note-on/off regression tests.
 *
 * Firmware facts (verified in Clock.ino and Midi.ino):
 *   - MIDI_DRUMNOTES_OUT is #if 0 (features.h:3), so the ONLY MIDI note traffic
 *     is from EXT_INST.  All note assertions can be made unambiguously.
 *   - MidiSendNoteOn/Off add +12 to the note (Midi.ino:36,43).
 *     EXT_TRACK_NOTES[track] = 36+track, so wire note = 48+track (0x30+track).
 *   - extTrack[] is polyphonic: multiple tracks can fire on the same step.
 *   - InitMidiNoteOff() turns off all active notes at the START of each step
 *     (before the new step's notes are sent), so note-offs arrive before note-ons
 *     of the next step.
 *   - MIDI_ACCENT_VELOCITY = 16 (known bug — pinned, not fixed, DL-015).
 *     Steps with TOTAL_ACC set get velocity 16 instead of the higher expected value.
 *
 * Test fixture (FX_PTRN_EXT):
 *   - extTrack[0] and extTrack[3] active on steps 0 and 8
 *   - TOTAL_ACC (bit 12) set on step 8 → velocity = 16 (pinned bug)
 *   - EXTchannel = 2
 *
 * Expected wire notes:
 *   track 0 → EXT_TRACK_NOTES[0]+12 = 36+12 = 48 (0x30)
 *   track 3 → EXT_TRACK_NOTES[3]+12 = 39+12 = 51 (0x33)
 */
#include "test_runner.h"
#include "frontpanel.h"
#include "event_log.h"
#include "nava_sim.h"
#include "midi.h"
#include "patterns.h"

#include <stdio.h>
#include <stdlib.h>

#define BOOT_CYCLES   64000000ULL
#define STEP_CYCLES   (NAVA_PPQN_PERIOD_CYCLES * 24ULL)   /* 2000064 */
#define BAR_CYCLES    (16ULL * STEP_CYCLES)

/* EXT channel is 2; wire notes for track 0 and 3 */
#define EXT_CH      2u
#define WIRE_T0   0x30u   /* track 0: 36+12 = 48 */
#define WIRE_T3   0x33u   /* track 3: 39+12 = 51 */

static void test_ext_note_on_polyphonic(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    /* Anchor the window BEFORE the press.  fp_press/fp_release together advance
     * ~400k cycles, and the sequencer starts the moment the press is scanned,
     * so step 0 can fire before fp_release_button returns — anchoring t0 after
     * the release puts step 0's note-ons outside the search window entirely. */
    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);

    /* Run one full bar (16 steps) */
    nava_sim_run_cycles(ctx, BAR_CYCLES + STEP_CYCLES);

    /* Step 0 should produce note-ons for both track 0 and track 3 */
    const sim_event_t *on_t0 = assert_midi_note_on("ext/step0/track0",
                                                     &ctx->log, EXT_CH, WIRE_T0,
                                                     t0, t0 + STEP_CYCLES * 3);
    const sim_event_t *on_t3 = assert_midi_note_on("ext/step0/track3",
                                                     &ctx->log, EXT_CH, WIRE_T3,
                                                     t0, t0 + STEP_CYCLES * 3);

    /* Both note-ons must be polyphonic — they should appear within the same step */
    if (on_t0 && on_t3) {
        uint64_t delta = on_t3->cycle > on_t0->cycle
                         ? on_t3->cycle - on_t0->cycle
                         : on_t0->cycle - on_t3->cycle;
        /* Both notes for the same step must land within one PPQN tick */
        assert_cycle_within("ext/step0/polyphonic_delta",
                             delta, 0, NAVA_PPQN_PERIOD_CYCLES);
    }
}

static void test_ext_note_off_at_next_step(nava_sim_t *ctx) {
    /* InitMidiNoteOff() fires at the start of every step, turning off notes
     * from the previous step.  Note-offs for step-0 notes must appear before
     * or at the start of step 1. */
    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    uint64_t t0 = ctx->avr->cycle;

    nava_sim_run_cycles(ctx, BAR_CYCLES + STEP_CYCLES);

    /* Note-offs for track 0 (step 0) should appear between step 0 and step 2 */
    uint64_t step1_start = t0 + STEP_CYCLES;
    const sim_event_t *off_t0 = assert_midi_note_off("ext/note_off/track0",
                                                       &ctx->log, EXT_CH, WIRE_T0,
                                                       t0, t0 + STEP_CYCLES * 3);
    if (off_t0) {
        /* Note-off must appear at or after the step-0 note-on, and no later
         * than step 2's start. */
        if (off_t0->cycle > step1_start + STEP_CYCLES) {
            test_fail("ext/note_off/timing",
                      "note-off for track0 at cycle %llu, too late (step2=%llu)",
                      (unsigned long long)off_t0->cycle,
                      (unsigned long long)(step1_start + STEP_CYCLES));
        }
    }
    (void)step1_start;
}

static void test_ext_accent_velocity_pinned_bug(nava_sim_t *ctx) {
    /* Step 8 has TOTAL_ACC set → Clock.ino assigns velocity=MIDI_ACCENT_VELOCITY=16.
     * This is a PINNED BUG (DL-015): the velocity should be higher but is 16.
     * Test asserts the CURRENT behavior (16), not the desired behavior. */
    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    uint64_t t0 = ctx->avr->cycle;

    /* Run 10 steps so step 8 fires */
    nava_sim_run_cycles(ctx, STEP_CYCLES * 10 + STEP_CYCLES);

    uint64_t step8_lo = t0 + STEP_CYCLES * 8 - STEP_CYCLES / 2;
    uint64_t step8_hi = t0 + STEP_CYCLES * 9 + STEP_CYCLES / 2;

    /* Find the note-on for track 0 at step 8 */
    const sim_event_t *accent_on = nava_midi_expect_note_on(&ctx->log,
                                                             EXT_CH, WIRE_T0,
                                                             step8_lo, step8_hi);
    if (!accent_on) {
        test_fail("ext/accent_bug",
                  "no note-on for track0 at step 8 (accent step)");
        return;
    }

    /* PINNED BUG: velocity must be exactly FX_MIDI_ACCENT_VEL = 16 */
    if (accent_on->midi_note.velocity != FX_MIDI_ACCENT_VEL) {
        test_fail("ext/accent_bug",
                  "PINNED BUG changed: expected velocity=%u got %u "
                  "(update pin if bug is fixed)",
                  FX_MIDI_ACCENT_VEL, accent_on->midi_note.velocity);
    } else {
        printf("# PINNED BUG confirmed: MIDI_ACCENT_VELOCITY=%u "
               "(expected 16 — quieter than normal)\n", FX_MIDI_ACCENT_VEL);
    }
}

static void test_ext_notes_only_midi_traffic(nava_sim_t *ctx) {
    /* Since MIDI_DRUMNOTES_OUT is compiled out, the ONLY EVT_MIDI_NOTE_ON
     * events should be EXT_INST notes.  Verify no unknown notes appear. */
    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    uint64_t t0 = ctx->avr->cycle;
    nava_sim_run_cycles(ctx, BAR_CYCLES);

    for (size_t i = 0; i < ctx->log.count; i++) {
        const sim_event_t *e = &ctx->log.buf[i];
        if (e->type != EVT_MIDI_NOTE_ON) continue;
        if (e->cycle < t0) continue;
        /* All EXT_INST notes must be in range 0x30-0x3F (48-63) */
        if (e->midi_note.note < 0x30u || e->midi_note.note > 0x3Fu) {
            test_fail("ext/only_ext_notes",
                      "unexpected MIDI note-on: wire_note=0x%02X ch=%u "
                      "(expected 0x30-0x3F range only; "
                      "drum-note MIDI is compiled out)",
                      e->midi_note.note, e->midi_note.channel);
        }
    }
    (void)t0;
}

int main(void) {
    TEST_WITH_PATTERN("ext_inst_polyphonic_note_on",
                      test_ext_note_on_polyphonic, &FX_PTRN_EXT, 2, 1);
    TEST_WITH_PATTERN("ext_inst_note_off_at_next_step",
                      test_ext_note_off_at_next_step, &FX_PTRN_EXT, 2, 1);
    TEST_WITH_PATTERN("ext_inst_accent_velocity_pinned_bug",
                      test_ext_accent_velocity_pinned_bug, &FX_PTRN_EXT, 2, 1);
    TEST_WITH_PATTERN("ext_inst_no_drum_notes_in_midi",
                      test_ext_notes_only_midi_traffic, &FX_PTRN_EXT, 2, 1);

    return test_run_all(NAVA_ELF_PATH);
}
