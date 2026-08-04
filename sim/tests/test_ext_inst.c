/* sim/tests/test_ext_inst.c — CI-093
 * EXT_INST MIDI note-on/off regression tests.
 *
 * Firmware facts (verified in Clock.ino and Midi.ino):
 *   - MIDI_DRUMNOTES_OUT is #if 0 (features.h:3), so the ONLY MIDI note traffic
 *     is from EXT_INST.  All note assertions can be made unambiguously.
 *   - The ext path sends through MidiSendExtNoteOn/Off, which do NOT apply the +12
 *     that MidiSendNoteOn/Off add for drum notes.  extTrackNote[track] holds the
 *     literal wire note and defaults to EXT_TRACK_NOTES[track] = 48+track (0x30+track),
 *     so unedited tracks transmit the same pitches the old fixed table did.
 *   - The LCD renders those notes as names under MIDI 60 = C4, so 48 reads C3.
 *   - extTrack[] is polyphonic: multiple tracks can fire on the same step.
 *   - CountPPQN() transmits each step itself when the UART has room, falling back to
 *     ServiceExtMidiNotes() in loop() only for bursts too dense for the TX ring.  The
 *     previous step's note-offs go out EXT_RELEASE_LEAD ticks before the next step
 *     rather than at it, so the step boundary carries note-ons only; note-offs still
 *     precede the next step's note-ons, just earlier.
 *   - Each ext step carries one of two velocity levels, per track: extAccent[track]
 *     bit set → MIDI_HIGH_VELOCITY (111), clear → MIDI_LOW_VELOCITY (63).  TOTAL_ACC
 *     adds MIDI_ACCENT_VELOCITY (16) on top of either, so an accented step on a
 *     TOTAL_ACC beat sends 127.
 *
 * Test fixture (FX_PTRN_EXT):
 *   - extTrack[0] and extTrack[3] active on steps 0 and 8, both fully accented
 *   - TOTAL_ACC (bit 12) set on step 8 → velocity = 127
 *   - EXTchannel = 2
 *
 * Expected wire notes:
 *   track 0 → EXT_TRACK_NOTES[0] = 48 (0x30), displayed C3
 *   track 3 → EXT_TRACK_NOTES[3] = 51 (0x33), displayed D#3
 */
#include "test_runner.h"
#include "frontpanel.h"
#include "event_log.h"
#include "nava_sim.h"
#include "midi.h"
#include "gpio.h"
#include "lcd.h"
#include "patterns.h"

#include <stdio.h>
#include <stdlib.h>

/* Boot budget: ~6 s simulated at 16 MHz. Boot itself measures ~4.25 s (68M
 * cycles): the panel fill animation is ~1.3 s of it, then the 2 s version splash,
 * and the panel is not scanned until both are done. */
#define BOOT_CYCLES   96000000ULL
#define STEP_CYCLES   (NAVA_PPQN_PERIOD_CYCLES * 24ULL)   /* 2000064 */
#define BAR_CYCLES    (16ULL * STEP_CYCLES)

/* EXT channel is 2; default wire notes for track 0 and 3 */
#define EXT_CH      2u
#define WIRE_T0   0x30u   /* track 0: 48, shown as C3  */
#define WIRE_T3   0x33u   /* track 3: 51, shown as D#3 */

/* Step programmed by the front-panel test.  FX_PTRN_BASIC has extTrack[] empty, so
 * any step works; 4 is far enough into the bar to be unambiguous. */
#define PROG_STEP 4

/* GUIDE latches sequenced ext MIDI output and boots unlatched, so every test that
 * expects the sequencer to transmit has to arm it first.  Bare press only — SHIFT and
 * INST qualify GUIDE as the edit-mode enter and exit gestures and do not toggle it. */
static void latch_guide(nava_sim_t *ctx) {
    fp_press_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_GUIDE);
    fp_settle(ctx);
}

static void test_ext_note_on_polyphonic(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    latch_guide(ctx);   /* sequenced ext output is off until GUIDE is latched */
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
    /* Every step queues a note-off request that ServiceExtMidiNotes() drains from
     * loop(), turning off notes from the previous step.  Note-offs for step-0 notes
     * must appear before or at the start of step 1. */
    boot_wait_ready(ctx, BOOT_CYCLES);
    latch_guide(ctx);   /* sequenced ext output is off until GUIDE is latched */
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

static void test_ext_accent_velocity(nava_sim_t *ctx) {
    /* Step 8 has TOTAL_ACC set → Clock.ino assigns
     * velocity = MIDI_HIGH_VELOCITY + MIDI_ACCENT_VELOCITY = 127, i.e. accent is
     * louder than an unaccented step rather than quieter. */
    boot_wait_ready(ctx, BOOT_CYCLES);
    latch_guide(ctx);   /* sequenced ext output is off until GUIDE is latched */
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
        test_fail("ext/accent",
                  "no note-on for track0 at step 8 (accent step)");
        return;
    }

    if (accent_on->midi_note.velocity != FX_MIDI_ACCENT_VEL) {
        test_fail("ext/accent",
                  "accented step: expected velocity=%u got %u",
                  FX_MIDI_ACCENT_VEL, accent_on->midi_note.velocity);
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

/* Front-panel regression for ext step programming.
 *
 * Drives the real panel: SHIFT+GUIDE to enter EXT INST edit mode, then a bare step
 * button to program a step on the selected track (track 0 by default), then PLAY,
 * and asserts the sequencer emits that track's note when the step comes round.
 *
 * This is the case none of the MIDI-level tests could see, because they seed
 * extTrack[] through the EEPROM fixture and never press a step button.  The bug it
 * pins: key.ino tested stepBtn[].justPressed, whose only live setter is InstValueGet,
 * which SeqParameter stops calling once edit mode owns the step buttons — so the flag
 * was always 0 and programming never executed.  The fixture starts with extTrack[]
 * empty, so a note-on here can only come from the button press.
 */
static void test_ext_step_programming_via_panel(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    latch_guide(ctx);   /* sequenced ext output is off until GUIDE is latched */

    /* Enter EXT INST edit mode: SHIFT held, GUIDE tapped. */
    fp_press_button(ctx, FP_BTN_SHIFT);
    fp_press_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_SHIFT);
    fp_settle(ctx);

    /* The 800ms splash owns the LCD; let it expire so the mode line is observable
     * and so nothing about the splash overlaps the programming press. */
    nava_sim_run_cycles(ctx, 16000000ULL);   /* 1s at 16MHz */
    /* Edit mode retitles the last header column from the instrument label to the
     * selected track, and puts that track's MIDI note in the value field below.
     * "T1 " with the trailing pad, not a bare "T": LT/MT/HT/EXT all contain a T and
     * would match with the mode not entered. */
    assert_lcd_contains("ext/panel/edit_mode_entered", ctx, 0, "ptr len scl T1 ");
    /* Track 1 defaults to wire note 48 — the same pitch the fixed table transmitted. */
    assert_lcd_contains("ext/panel/note_shown", ctx, 1, "C3");

    /* Program step PROG_STEP on the selected track (track 0). Paused, the step buttons
     * are track switches, so a programming press is qualified with INST. */
    fp_press_button(ctx, FP_BTN_INST);
    fp_press_step(ctx, PROG_STEP);
    fp_release_step(ctx, PROG_STEP);
    fp_release_button(ctx, FP_BTN_INST);
    fp_settle(ctx);

    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);

    /* Run two full bars so the programmed step fires regardless of where in the bar
     * the transport started. */
    nava_sim_run_cycles(ctx, BAR_CYCLES * 2);

    assert_midi_note_on("ext/panel/programmed_step_sounds",
                        &ctx->log, EXT_CH, WIRE_T0,
                        t0, t0 + BAR_CYCLES * 2);
}

/* The encoder retunes the selected track, and playback transmits the new note.
 *
 * Two assertions that must hold together: the LCD has to show the new value, and the
 * sequencer has to actually send it.  Asserting only the display would pass with the
 * note map edited but ServiceExtMidiNotes() still reading the fixed PROGMEM table.
 * The absence check on the old pitch is what pins that — a firmware that ignores the
 * map keeps emitting 48 and would otherwise satisfy a note-on-only assertion by luck.
 */
static void test_ext_encoder_sets_track_note(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    latch_guide(ctx);   /* sequenced ext output is off until GUIDE is latched */

    fp_press_button(ctx, FP_BTN_SHIFT);
    fp_press_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_SHIFT);
    fp_settle(ctx);
    nava_sim_run_cycles(ctx, 16000000ULL);   /* outlast the 800ms splash */

    /* EncGet needs two detents per increment (Enc.ino debounces a jumpy encoder), so
     * 8 detents is +4 semitones: 48 -> 52. */
    nava_gpio_inject_encoder(ctx->gpio, +1, 8);
    fp_settle(ctx);
    assert_lcd_contains("ext/encoder/note_raised", ctx, 1, "E3");

    fp_press_button(ctx, FP_BTN_INST);   /* paused: INST qualifies a programming press */
    fp_press_step(ctx, PROG_STEP);
    fp_release_step(ctx, PROG_STEP);
    fp_release_button(ctx, FP_BTN_INST);
    fp_settle(ctx);

    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, BAR_CYCLES * 2);

    assert_midi_note_on("ext/encoder/retuned_note_sounds",
                        &ctx->log, EXT_CH, 0x34u /* 52 */,
                        t0, t0 + BAR_CYCLES * 2);

    if (nava_midi_expect_note_on(&ctx->log, EXT_CH, WIRE_T0, t0, t0 + BAR_CYCLES * 2)) {
        test_fail("ext/encoder/old_note_silent",
                  "track still transmitted its default note 48 after retune to 52");
    }
}

/* Holding SHUFFLE hands the encoder to the shuffle amount, even inside ext edit mode.
 *
 * The edit mode used to keep the encoder on the track note whatever else was held, so
 * the one parameter the held button implies was the one it could not reach.  Asserted
 * both ways: the note must be untouched, and the shuffle must actually have moved -
 * checking only the first would pass on an encoder that does nothing at all. */
static void test_ext_shuffle_button_owns_encoder(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);

    fp_press_button(ctx, FP_BTN_SHIFT);
    fp_press_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_SHIFT);
    fp_settle(ctx);
    nava_sim_run_cycles(ctx, 16000000ULL);   /* outlast the 800ms splash */

    /* Two detents per increment, so 8 detents is +4: FX_PTRN_BASIC boots at shuffle=1
     * and lands on 5, whose table entry is {0,-4} - odd steps 4 PPQN ticks early. */
    fp_press_button(ctx, FP_BTN_SHUF);
    nava_gpio_inject_encoder(ctx->gpio, +1, 8);
    fp_settle(ctx);
    fp_release_button(ctx, FP_BTN_SHUF);
    fp_settle(ctx);

    /* Track 1 is selected on entry and defaults to note 48 */
    assert_lcd_contains("ext/shuf_encoder/note_untouched", ctx, 1, "C3");

    event_log_clear(&ctx->log);
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    uint64_t t0 = ctx->avr->cycle;
    nava_sim_run_cycles(ctx, BAR_CYCLES);

    const sim_event_t *step0 = event_log_find_step_onset(&ctx->log, t0, 0);
    const sim_event_t *step1 = event_log_find_step_onset(&ctx->log, t0, 1);
    if (!step0 || !step1) {
        test_fail("ext/shuf_encoder/onsets", "need 2 step onsets to measure shuffle");
        return;
    }
    assert_cycle_within("ext/shuf_encoder/shuffle_applied",
                        step1->cycle - step0->cycle,
                        20ULL * NAVA_PPQN_PERIOD_CYCLES,   /* 24 - 4 */
                        NAVA_PPQN_PERIOD_CYCLES / 2);
}

/* Enter ext edit mode and outlast the 800ms entry splash. */
static void enter_ext_edit(nava_sim_t *ctx) {
    fp_press_button(ctx, FP_BTN_SHIFT);
    fp_press_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_SHIFT);
    fp_settle(ctx);
    nava_sim_run_cycles(ctx, 16000000ULL);
}

static size_t count_ext_note_ons(const event_log_t *log, uint8_t wire,
                                 uint64_t from, uint64_t to) {
    size_t n = 0;
    for (size_t i = 0; i < log->count; i++) {
        const sim_event_t *e = &log->buf[i];
        if (e->cycle < from || e->cycle >= to) continue;
        if (e->type != EVT_MIDI_NOTE_ON) continue;
        if (e->midi_note.channel != EXT_CH || e->midi_note.note != wire) continue;
        if (e->midi_note.velocity == 0) continue;
        n++;
    }
    return n;
}

/* The two programmable velocity levels, and their independence per track.
 *
 * FX_PTRN_EXT_LEVELS puts track 0 accented and track 3 unaccented on the SAME step,
 * which is the assertion a shared per-step velocity cannot satisfy: it would emit one
 * value for both. Step 8 then repeats track 0 unaccented, so the same track is observed
 * at both levels within one bar - a firmware that ignored extAccent entirely and sent a
 * constant velocity would fail there even if the step-0 pair happened to match. */
static void test_ext_two_velocity_levels(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    latch_guide(ctx);
    event_log_clear(&ctx->log);

    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, STEP_CYCLES * 10);

    uint64_t step0_hi = t0 + STEP_CYCLES * 3;
    uint64_t step8_lo = t0 + STEP_CYCLES * 8 - STEP_CYCLES / 2;
    uint64_t step8_hi = t0 + STEP_CYCLES * 9 + STEP_CYCLES / 2;

    const sim_event_t *loud = nava_midi_expect_note_on(&ctx->log, EXT_CH, WIRE_T0,
                                                        t0, step0_hi);
    const sim_event_t *soft = nava_midi_expect_note_on(&ctx->log, EXT_CH, WIRE_T3,
                                                        t0, step0_hi);
    const sim_event_t *same_track_soft = nava_midi_expect_note_on(&ctx->log, EXT_CH,
                                                                   WIRE_T0,
                                                                   step8_lo, step8_hi);
    if (!loud || !soft || !same_track_soft) {
        test_fail("ext/levels/notes_present",
                  "missing note-ons: step0/t0=%d step0/t3=%d step8/t0=%d",
                  loud != NULL, soft != NULL, same_track_soft != NULL);
        return;
    }

    if (loud->midi_note.velocity != FX_MIDI_HIGH_VEL) {
        test_fail("ext/levels/accented_track",
                  "accented track on step 0: expected velocity=%u got %u",
                  FX_MIDI_HIGH_VEL, loud->midi_note.velocity);
    }
    if (soft->midi_note.velocity != FX_MIDI_LOW_VEL) {
        test_fail("ext/levels/per_track",
                  "unaccented track sharing step 0 with an accented one: "
                  "expected velocity=%u got %u (level leaked across tracks)",
                  FX_MIDI_LOW_VEL, soft->midi_note.velocity);
    }
    if (same_track_soft->midi_note.velocity != FX_MIDI_LOW_VEL) {
        test_fail("ext/levels/per_step",
                  "same track unaccented on step 8: expected velocity=%u got %u",
                  FX_MIDI_LOW_VEL, same_track_soft->midi_note.velocity);
    }
}

/* Programming the two levels from the panel: press once for the low level, again for
 * the high one, a third time to clear - the cycle InstValueGet() gives the drum
 * instruments, applied to the ext lane.
 *
 * Driven while the transport runs, where a bare step press programs (paused it would be
 * a track switch). The velocity of what the sequencer then transmits is the assertion:
 * an implementation that toggled the step on and off in two presses would never reach
 * the second level, and one that stored the level but did not transmit it would keep
 * sending the low velocity after the second press. */
static void test_ext_second_press_sets_high_level(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    latch_guide(ctx);
    enter_ext_edit(ctx);

    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, STEP_CYCLES);

    /* First press: step on, low level. */
    fp_press_step(ctx, PROG_STEP);
    fp_release_step(ctx, PROG_STEP);
    fp_settle(ctx);

    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;
    nava_sim_run_cycles(ctx, BAR_CYCLES * 2);
    const sim_event_t *low = nava_midi_expect_note_on(&ctx->log, EXT_CH, WIRE_T0,
                                                       t0, ctx->avr->cycle);
    if (!low) {
        test_fail("ext/press/first_press_programs",
                  "one press on step %d produced no note", PROG_STEP);
        return;
    }
    if (low->midi_note.velocity != FX_MIDI_LOW_VEL) {
        test_fail("ext/press/first_press_low",
                  "first press: expected velocity=%u got %u",
                  FX_MIDI_LOW_VEL, low->midi_note.velocity);
    }

    /* Second press: same step, high level. */
    fp_press_step(ctx, PROG_STEP);
    fp_release_step(ctx, PROG_STEP);
    fp_settle(ctx);

    event_log_clear(&ctx->log);
    uint64_t t1 = ctx->avr->cycle;
    nava_sim_run_cycles(ctx, BAR_CYCLES * 2);
    const sim_event_t *high = nava_midi_expect_note_on(&ctx->log, EXT_CH, WIRE_T0,
                                                        t1, ctx->avr->cycle);
    if (!high) {
        test_fail("ext/press/second_press_keeps_step",
                  "second press silenced the step instead of accenting it");
    }
    else if (high->midi_note.velocity != FX_MIDI_HIGH_VEL) {
        test_fail("ext/press/second_press_high",
                  "second press: expected velocity=%u got %u",
                  FX_MIDI_HIGH_VEL, high->midi_note.velocity);
    }

    /* Third press: step off. */
    fp_press_step(ctx, PROG_STEP);
    fp_release_step(ctx, PROG_STEP);
    fp_settle(ctx);

    event_log_clear(&ctx->log);
    uint64_t t2 = ctx->avr->cycle;
    nava_sim_run_cycles(ctx, BAR_CYCLES * 2);
    size_t after = count_ext_note_ons(&ctx->log, WIRE_T0, t2, ctx->avr->cycle);
    if (after != 0) {
        test_fail("ext/press/third_press_clears",
                  "third press left the step sounding %zu times", after);
    }
}

/* The ext velocity config page sets both levels, and playback uses what it set.
 *
 * End to end through the panel: TEMPO walks to the page, the encoder edits the field the
 * encoder button selects, and the transport (which drops config mode) then has to
 * transmit the edited pair. Asserting only the LCD would pass with the values stored and
 * Clock.ino still sending the compiled-in defaults.
 *
 * The two fields move in opposite directions on purpose: raising both would still pass if
 * the second field aliased the first.
 *
 * The expected velocities are read back off the page rather than computed from the
 * detent count: Enc.ino's debounce swallows an edge often enough that a fixed count is
 * off by one, and pinning the exact arithmetic of the encoder is not what this test is
 * for. Reading the display instead adds an assertion worth having - what the page shows
 * and what the wire carries have to be the same number. Both values must still have
 * MOVED off their defaults, or a firmware that ignored the encoder entirely would
 * satisfy the comparison trivially. */
#define EXT_VEL_DETENTS  20   /* ~10 increments; EncGet needs two detents per step */

/* SHIFT+TEMPO is the config gesture: the page handler lives in SeqParameter's
 * shift-held branch. SHIFT is held across the whole walk. */
static void walk_to_config_page(nava_sim_t *ctx, int page) {
    fp_press_button(ctx, FP_BTN_SHIFT);
    for (int i = 0; i < page; i++) {
        fp_press_button(ctx, FP_BTN_TEMPO);
        fp_release_button(ctx, FP_BTN_TEMPO);
        fp_settle(ctx);
    }
    fp_release_button(ctx, FP_BTN_SHIFT);
    fp_settle(ctx);
}

static void test_ext_velocity_config_page(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    latch_guide(ctx);

    /* SHIFT+TEMPO enters config mode on page 1 and advances one page per press. */
    walk_to_config_page(ctx, FX_CONF_PAGE_EXT_VEL);
    assert_lcd_contains("ext/velconf/page_reached", ctx, 0, "Low hi  ext vel");

    /* Field 0 (low) is selected on arrival: raise it. */
    nava_gpio_inject_encoder(ctx->gpio, +1, EXT_VEL_DETENTS);
    fp_settle(ctx);

    /* Encoder button moves the cursor to field 1 (high): lower it. */
    nava_gpio_set_encoder_switch(ctx->gpio, 1);
    fp_settle(ctx);
    nava_gpio_set_encoder_switch(ctx->gpio, 0);
    fp_settle(ctx);
    assert_lcd_contains("ext/velconf/cursor_moved", ctx, 0, "low Hi  ext vel");
    nava_gpio_inject_encoder(ctx->gpio, -1, EXT_VEL_DETENTS);
    fp_settle(ctx);

    /* Line 1 is "<low> <high> 1 / 2 tap" in 4-column fields. */
    const char *shown = nava_lcd_get_line(ctx->lcd, 1);
    unsigned shown_low = 0, shown_high = 0;
    if (!shown || sscanf(shown, "%u %u", &shown_low, &shown_high) != 2) {
        test_fail("ext/velconf/page_values_shown",
                  "could not read the two levels off the page: \"%s\"",
                  shown ? shown : "(no LCD output)");
        return;
    }
    if (shown_low <= FX_MIDI_LOW_VEL || shown_high >= FX_MIDI_HIGH_VEL) {
        test_fail("ext/velconf/encoder_moved_values",
                  "encoder did not move both levels off their defaults: "
                  "low %u (default %u), high %u (default %u)",
                  shown_low, FX_MIDI_LOW_VEL, shown_high, FX_MIDI_HIGH_VEL);
        return;
    }
    uint8_t want_low  = (uint8_t)shown_low;
    uint8_t want_high = (uint8_t)shown_high;

    /* Starting the transport drops config mode (Seq.ino refuses config while running). */
    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, STEP_CYCLES * 3);

    const sim_event_t *accented = nava_midi_expect_note_on(&ctx->log, EXT_CH, WIRE_T0,
                                                            t0, ctx->avr->cycle);
    const sim_event_t *plain = nava_midi_expect_note_on(&ctx->log, EXT_CH, WIRE_T3,
                                                         t0, ctx->avr->cycle);
    if (!accented || !plain) {
        test_fail("ext/velconf/notes_present",
                  "step 0 produced no notes after the config edit (t0=%d t3=%d)",
                  accented != NULL, plain != NULL);
        return;
    }
    if (accented->midi_note.velocity != want_high) {
        test_fail("ext/velconf/high_level_applied",
                  "double-tap level: expected velocity=%u got %u",
                  want_high, accented->midi_note.velocity);
    }
    if (plain->midi_note.velocity != want_low) {
        test_fail("ext/velconf/low_level_applied",
                  "single-tap level: expected velocity=%u got %u",
                  want_low, plain->midi_note.velocity);
    }
}

/* LAST STEP inside ext edit mode sets the EXT layer's last step, not the sequencer's.
 *
 * Both halves are asserted, because either alone passes for the wrong reason: that the
 * ext lane got shorter is equally true of the bug (which truncated the whole pattern and
 * dragged the ext lane along), and that the pattern is intact is trivially true if
 * LAST STEP did nothing at all.
 *
 * FX_PTRN_EXT fires ext track 0 on steps 0 and 8 and puts a drum trigger on those same
 * two steps. Setting the ext last step to 3 makes the ext lane cycle 0..3, so track 0
 * sounds every 4 steps - 4 times a bar instead of 2 - while the kit must still reach
 * step 8, which it can only do if pattern.length was left alone. */
static void test_ext_last_step_scoped_to_ext_layer(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    latch_guide(ctx);
    enter_ext_edit(ctx);

    fp_press_button(ctx, FP_BTN_LASTSTEP);
    fp_press_step(ctx, 3);              /* step button 4 -> last step index 3 */
    fp_release_step(ctx, 3);
    fp_release_button(ctx, FP_BTN_LASTSTEP);
    fp_settle(ctx);

    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, BAR_CYCLES);

    size_t ons = count_ext_note_ons(&ctx->log, WIRE_T0, t0, t0 + BAR_CYCLES);
    printf("# ext_last_step: track0 note-ons in one bar = %zu (expect ~4)\n", ons);
    if (ons < 3) {
        test_fail("ext/last_step/ext_loops_short",
                  "ext lane sounded %zu times in a bar; a 4-step ext loop should fire ~4",
                  ons);
    }

    /* Count drum triggers instead of looking for one near step 8. FX_PTRN_EXT puts a
     * trigger on steps 0 and 8 of a 16-step pattern, so an intact sequencer fires twice
     * a bar. Truncating pattern.length to 3 makes it fire on curStep 0 of a 4-step loop
     * - absolute steps 0, 4, 8, 12 - which still puts a trigger near step 8 and would
     * satisfy a "did it reach step 8" check while being exactly the bug. The COUNT is
     * what separates them: 2 when scoped correctly, 4 when the sequencer was truncated. */
    size_t onsets = 0;
    for (int i = 0; i < 40; i++) {
        const sim_event_t *e = event_log_find_step_onset(&ctx->log, t0, i);
        if (!e || e->cycle >= t0 + BAR_CYCLES) break;
        onsets++;
    }
    printf("# ext_last_step: drum onsets in one bar = %zu (expect 2)\n", onsets);
    if (onsets > 3) {
        test_fail("ext/last_step/pattern_length_intact",
                  "%zu drum triggers in a bar; an intact 16-step pattern fires 2, a "
                  "pattern truncated to 4 steps fires 4 - LAST STEP hit the sequencer",
                  onsets);
    }
}

/* The running playhead in ext edit mode chases the EXT lane, not the drum lane.
 *
 * PTRN_STEP flashes the step LEDs at curStep over the selected instrument's content;
 * ext edit mode is the same display one layer down, so its chase has to run over the
 * lane it is showing. The two positions are identical until LAST STEP shortens the ext
 * layer, which is why the test shortens it to 4 steps and leaves the kit at 16.
 *
 * FX_PTRN_BASIC has extTrack[] empty, so with no content underneath the step LED word is
 * the playhead alone: the union over a bar is exactly the set of positions it visited.
 * 0x000F is the 4-step ext loop; 0xFFFF is a chase driven from the 16-step drum lane,
 * and a frozen chase collapses to one bit. Sampling at quarter-step resolution catches
 * every step at both blinkFast phases. */
static void test_ext_playhead_follows_ext_lane(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    enter_ext_edit(ctx);

    fp_press_button(ctx, FP_BTN_LASTSTEP);
    fp_press_step(ctx, 3);              /* step button 4 -> ext last step index 3 */
    fp_release_step(ctx, 3);
    fp_release_button(ctx, FP_BTN_LASTSTEP);
    fp_settle(ctx);

    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);

    uint16_t visited = 0;
    for (int i = 0; i < 64; i++) {      /* one 16-step bar = four ext loops */
        nava_sim_run_cycles(ctx, STEP_CYCLES / 4);
        visited |= fp_step_leds(ctx);
    }

    printf("# ext_playhead: steps visited in one bar = 0x%04X (expect 0x000F)\n", visited);
    if (visited != 0x000Fu) {
        test_fail("ext/playhead/follows_ext_lane",
                  "playhead visited 0x%04X over a bar; a 4-step ext loop visits 0x000F "
                  "(0xFFFF = chasing the 16-step drum lane, one bit = frozen)", visited);
    }
}

/* Holding LAST STEP in ext edit mode lights the ext layer's last step.
 *
 * The step LEDs otherwise show the selected track's content, so the assertion is exact
 * equality with a single bit rather than "bit 3 is lit": FX_PTRN_EXT has track 0 on steps
 * 0 and 8, and the default extLength is 15, so the three candidate displays (0x0008,
 * 0x0101, 0x8000) are mutually exclusive and a wrong one cannot pass.
 *
 * Sampled with the transport stopped. Running, the playhead flash would XOR into the word
 * on some passes, and this display deliberately suppresses it - but a test that had to
 * tolerate the flash could not assert equality at all. */
static void test_ext_last_step_led_shows_ext_length(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    enter_ext_edit(ctx);

    fp_press_button(ctx, FP_BTN_LASTSTEP);
    fp_press_step(ctx, 3);              /* step button 4 -> ext last step index 3 */
    fp_release_step(ctx, 3);
    fp_settle(ctx);
    /* Still holding LAST STEP: this is the display under test. Clear the log first so
     * fp_step_leds() reads a write made under the hold, not one from before it. */
    event_log_clear(&ctx->log);
    nava_sim_run_cycles(ctx, 4000000ULL);
    uint16_t held = fp_step_leds(ctx);
    fp_release_button(ctx, FP_BTN_LASTSTEP);
    fp_settle(ctx);

    if (held != (uint16_t)(1u << 3)) {
        test_fail("ext/last_step_led/shows_ext_length",
                  "step LEDs under LAST STEP = 0x%04X, expected 0x0008 (ext last step 3; "
                  "0x0101 = track content, 0x8000 = unedited 16-step length)", held);
    }

    /* Releasing must hand the LEDs back to the track, or the display would be stuck. */
    event_log_clear(&ctx->log);
    nava_sim_run_cycles(ctx, 4000000ULL);
    uint16_t released = fp_step_leds(ctx);
    if (released == (uint16_t)(1u << 3)) {
        test_fail("ext/last_step_led/released",
                  "step LEDs still 0x%04X after LAST STEP was released", released);
    }
}

/* CLEAR inside ext edit mode clears the selected ext track, not the analog instrument.
 *
 * The old behaviour cleared inst[EXT_INST] - a word the ext playback path never reads -
 * so the track kept sounding. Held across a bar, the ext note-ons must stop; the drum
 * triggers, which CLEAR must not have touched, must not. */
static void test_ext_clear_scoped_to_ext_track(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    latch_guide(ctx);
    enter_ext_edit(ctx);

    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, BAR_CYCLES);

    size_t before = count_ext_note_ons(&ctx->log, WIRE_T0, t0, t0 + BAR_CYCLES);
    if (before == 0) {
        test_fail("ext/clear/precondition", "track 0 never sounded before CLEAR");
        return;
    }

    /* Hold CLEAR for two bars so the playhead passes every programmed step. */
    fp_press_button(ctx, FP_BTN_CLEAR);
    nava_sim_run_cycles(ctx, BAR_CYCLES * 2);
    fp_release_button(ctx, FP_BTN_CLEAR);

    uint64_t t1 = ctx->avr->cycle;
    event_log_clear(&ctx->log);
    nava_sim_run_cycles(ctx, BAR_CYCLES);

    size_t after = count_ext_note_ons(&ctx->log, WIRE_T0, t1, t1 + BAR_CYCLES);
    printf("# ext_clear: track0 note-ons before=%zu after=%zu\n", before, after);
    if (after != 0) {
        test_fail("ext/clear/track_cleared",
                  "track 0 still sounded %zu times after holding CLEAR", after);
    }

    /* CLEAR belonged to the ext track, so the drum grid must be untouched. */
    if (!event_log_find_step_onset(&ctx->log, t1, 0)) {
        test_fail("ext/clear/drums_intact",
                  "no drum triggers after CLEAR - it cleared the analog layer too");
    }
}

/* Leaving edit mode returns to the instrument that was selected on the way in.
 *
 * SD is chosen over BD deliberately: BD is the old hardcoded fallback, so restoring to
 * BD would look identical to not restoring at all.
 */
static void test_ext_exit_restores_instrument(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);

    /* SD_BTN is step index 2 (define.h): BD, BD_LOW, SD, SD_LOW ... */
    fp_press_button(ctx, FP_BTN_INST);
    fp_press_step(ctx, 2);
    fp_release_step(ctx, 2);
    fp_release_button(ctx, FP_BTN_INST);
    fp_settle(ctx);
    assert_lcd_contains("ext/exit/inst_selected", ctx, 0, "ptr len scl ins");
    assert_lcd_contains("ext/exit/sd_selected", ctx, 1, "SD");

    fp_press_button(ctx, FP_BTN_SHIFT);
    fp_press_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_SHIFT);
    fp_settle(ctx);
    nava_sim_run_cycles(ctx, 16000000ULL);
    assert_lcd_contains("ext/exit/mode_entered", ctx, 0, "ptr len scl T1 ");

    /* SHIFT+GUIDE again toggles back out. */
    fp_press_button(ctx, FP_BTN_SHIFT);
    fp_press_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_SHIFT);
    fp_settle(ctx);
    nava_sim_run_cycles(ctx, 16000000ULL);

    assert_lcd_contains("ext/exit/header_restored", ctx, 0, "ptr len scl ins");
    assert_lcd_contains("ext/exit/instrument_restored", ctx, 1, "SD");
}

/* Paused, a bare step press is a track switch: it selects the track and sustains its
 * note until release, so the note map can be auditioned by ear without holding INST.
 *
 * The note-off assertion is the load-bearing half. A preview that starts but is never
 * retired leaves the external synth droning, and that failure is invisible to any
 * note-on-only test — this is the same class of bug the sustained-preview guard in
 * ExtInstUpdate() exists to prevent.
 */
static void test_ext_track_switch_auditions_when_paused(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);

    fp_press_button(ctx, FP_BTN_SHIFT);
    fp_press_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_SHIFT);
    fp_settle(ctx);
    nava_sim_run_cycles(ctx, 16000000ULL);   /* outlast the 800ms splash */

    /* Transport is stopped — nothing has pressed PLAY. */
    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;

    fp_press_step(ctx, 3);                   /* track 4, default wire note 51 */
    fp_settle(ctx);
    assert_midi_note_on("ext/switch/auditions", &ctx->log, EXT_CH, WIRE_T3,
                        t0, ctx->avr->cycle);
    assert_lcd_contains("ext/switch/track_selected", ctx, 0, "ptr len scl T4 ");

    uint64_t t1 = ctx->avr->cycle;
    fp_release_step(ctx, 3);
    fp_settle(ctx);
    assert_midi_note_off("ext/switch/released", &ctx->log, EXT_CH, WIRE_T3,
                         t1, ctx->avr->cycle);
}

/* GUIDE is the master enable for sequenced ext MIDI, and it boots unlatched.
 *
 * Both halves matter. The silent half alone would pass on a firmware that never
 * transmits at all, and the sounding half alone would pass on one that ignores the
 * latch — so the test plays the same pattern twice across a single GUIDE press.
 */
static void test_ext_guide_gates_output(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);

    /* Unlatched: the fixture's extTrack[] steps must produce nothing. */
    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, BAR_CYCLES);

    if (nava_midi_expect_note_on(&ctx->log, EXT_CH, WIRE_T0, t0, ctx->avr->cycle)) {
        test_fail("ext/guide/silent_when_unlatched",
                  "sequencer transmitted ext notes with GUIDE unlatched");
    }

    fp_press_button(ctx, FP_BTN_STOP);
    fp_release_button(ctx, FP_BTN_STOP);
    fp_settle(ctx);

    /* Latched: the same pattern must now sound. */
    latch_guide(ctx);
    event_log_clear(&ctx->log);
    uint64_t t1 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, BAR_CYCLES);

    assert_midi_note_on("ext/guide/sounds_when_latched", &ctx->log, EXT_CH, WIRE_T0,
                        t1, ctx->avr->cycle);
}

/* Unlatching GUIDE mid-playback must silence what is already sounding.
 *
 * Dropping note-ons is not enough on its own: the note held from the last transmitted
 * step would stay held on the external synth with no later note-off to close it, since
 * the drain that would have sent one is now gated off.
 */
static void test_ext_guide_unlatch_silences(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    latch_guide(ctx);

    /* Unlatch while step 0's note is still held. Every step queues a note-off for the
     * previous one, so waiting a whole step would find nothing sounding and the test
     * would pass against a firmware that silences nothing. latch_guide() costs about
     * 640k cycles and a step is 2000064, so the toggle lands inside step 0. */
    event_log_clear(&ctx->log);
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, STEP_CYCLES / 4);

    if (!nava_midi_expect_note_on(&ctx->log, EXT_CH, WIRE_T0, 0, ctx->avr->cycle)) {
        test_fail("ext/guide/unlatch_precondition",
                  "step 0 never sounded, so there is nothing to silence");
        return;
    }

    uint64_t t0 = ctx->avr->cycle;
    latch_guide(ctx);                 /* second press unlatches */
    nava_sim_run_cycles(ctx, STEP_CYCLES * 2);

    /* Something must have been silenced, and nothing new may start. */
    if (!nava_midi_expect_note_off(&ctx->log, EXT_CH, WIRE_T0, t0, ctx->avr->cycle) &&
        !nava_midi_expect_note_off(&ctx->log, EXT_CH, WIRE_T3, t0, ctx->avr->cycle)) {
        test_fail("ext/guide/unlatch_note_off",
                  "unlatching GUIDE sent no note-off for the sounding step");
    }
    if (nava_midi_expect_note_on(&ctx->log, EXT_CH, WIRE_T0, t0, ctx->avr->cycle)) {
        test_fail("ext/guide/unlatch_stops_notes",
                  "sequencer kept transmitting after GUIDE was unlatched");
    }
}

int main(void) {
    TEST_WITH_PATTERN("ext_inst_step_programming_via_panel",
                      test_ext_step_programming_via_panel, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ext_inst_polyphonic_note_on",
                      test_ext_note_on_polyphonic, &FX_PTRN_EXT, 2, 1);
    TEST_WITH_PATTERN("ext_inst_note_off_at_next_step",
                      test_ext_note_off_at_next_step, &FX_PTRN_EXT, 2, 1);
    TEST_WITH_PATTERN("ext_inst_accent_velocity",
                      test_ext_accent_velocity, &FX_PTRN_EXT, 2, 1);
    TEST_WITH_PATTERN("ext_inst_two_velocity_levels",
                      test_ext_two_velocity_levels, &FX_PTRN_EXT_LEVELS, 2, 1);
    TEST_WITH_PATTERN("ext_inst_second_press_sets_high_level",
                      test_ext_second_press_sets_high_level, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ext_inst_velocity_config_page",
                      test_ext_velocity_config_page, &FX_PTRN_EXT_LEVELS, 2, 1);
    TEST_WITH_PATTERN("ext_inst_no_drum_notes_in_midi",
                      test_ext_notes_only_midi_traffic, &FX_PTRN_EXT, 2, 1);
    TEST_WITH_PATTERN("ext_inst_encoder_sets_track_note",
                      test_ext_encoder_sets_track_note, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ext_inst_last_step_scoped_to_ext_layer",
                      test_ext_last_step_scoped_to_ext_layer, &FX_PTRN_EXT, 2, 1);
    TEST_WITH_PATTERN("ext_inst_playhead_follows_ext_lane",
                      test_ext_playhead_follows_ext_lane, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ext_inst_last_step_led_shows_ext_length",
                      test_ext_last_step_led_shows_ext_length, &FX_PTRN_EXT, 2, 1);
    TEST_WITH_PATTERN("ext_inst_clear_scoped_to_ext_track",
                      test_ext_clear_scoped_to_ext_track, &FX_PTRN_EXT, 2, 1);
    TEST_WITH_PATTERN("ext_inst_shuffle_button_owns_encoder",
                      test_ext_shuffle_button_owns_encoder, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ext_inst_exit_restores_instrument",
                      test_ext_exit_restores_instrument, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ext_inst_track_switch_auditions_when_paused",
                      test_ext_track_switch_auditions_when_paused, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ext_inst_guide_gates_output",
                      test_ext_guide_gates_output, &FX_PTRN_EXT, 2, 1);
    TEST_WITH_PATTERN("ext_inst_guide_unlatch_silences",
                      test_ext_guide_unlatch_silences, &FX_PTRN_EXT, 2, 1);

    return test_run_all(NAVA_ELF_PATH);
}
