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
 *   - CountPPQN() only queues each step's ext notes; ServiceExtMidiNotes(), called
 *     from loop(), sends the previous step's note-offs and then the new step's
 *     note-ons, so note-offs still arrive before the next step's note-ons but both
 *     are deferred by up to one main-loop period (~4.5ms).
 *   - An accented step sends MIDI_HIGH_VELOCITY + MIDI_ACCENT_VELOCITY = 127.
 *
 * Test fixture (FX_PTRN_EXT):
 *   - extTrack[0] and extTrack[3] active on steps 0 and 8
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
#include "patterns.h"

#include <stdio.h>
#include <stdlib.h>

#define BOOT_CYCLES   64000000ULL
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
    TEST_WITH_PATTERN("ext_inst_no_drum_notes_in_midi",
                      test_ext_notes_only_midi_traffic, &FX_PTRN_EXT, 2, 1);
    TEST_WITH_PATTERN("ext_inst_encoder_sets_track_note",
                      test_ext_encoder_sets_track_note, &FX_PTRN_BASIC, 2, 1);
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
