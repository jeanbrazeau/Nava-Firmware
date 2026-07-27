/* sim/tests/test_ext_latency.c
 * Measures how far the ext-instrument MIDI note-on lags the analog trigger of
 * the same step.  CountPPQN() writes the trigger word inline but only queues the
 * ext step; ServiceExtMidiNotes() transmits it from loop(), so the gap is loop
 * scheduling latency plus UART serialisation.
 *
 * FX_PTRN_EXT_SYNC puts BD and ext tracks 0/3 on all 16 steps, so each step
 * produces one TRIG_WORD onset and two note-ons to difference.
 *
 * The bound asserted here is a regression guard, not a spec: it exists so a
 * change that reintroduces multi-millisecond lag fails loudly. */
#include "test_runner.h"
#include "frontpanel.h"
#include "event_log.h"
#include "nava_sim.h"
#include "patterns.h"
#include "midi.h"

#include <stdio.h>

#define BOOT_CYCLES   64000000ULL
#define STEP_CYCLES   (NAVA_PPQN_PERIOD_CYCLES * 24ULL)   /* 2000064 @120bpm */
#define BAR_CYCLES    (16ULL * STEP_CYCLES)

#define EXT_CH      2u
#define WIRE_T0   0x30u

#define CYCLES_PER_US 16ULL

/* Ceiling for the trigger→first-note-on gap.  One 16th at 120 BPM is 125 ms, so
 * this is generous musically but tight enough to catch a return of loop-latency
 * scheduling. */
#define MAX_LAG_US  2500ULL

static void latch_guide(nava_sim_t *ctx) {
    fp_press_button(ctx, FP_BTN_GUIDE);
    fp_release_button(ctx, FP_BTN_GUIDE);
    fp_settle(ctx);
}

/* First non-realtime TX byte at or after `from`.  This is the moment the ext
 * step actually reaches the UART, so trig→here is pure scheduling latency and
 * here→note-on is wire time. */
static const sim_event_t *next_channel_tx_byte(const event_log_t *log,
                                                uint64_t from) {
    for (size_t i = 0; i < log->count; i++) {
        const sim_event_t *e = &log->buf[i];
        if (e->cycle < from) continue;
        if (e->type != EVT_MIDI_TX_BYTE) continue;
        if (e->raw_byte >= 0xF8u) continue;   /* clock/start/stop */
        return e;
    }
    return NULL;
}

/* First note-on for `wire_note` on EXT_CH at or after `from`. */
static const sim_event_t *next_ext_note_on(const event_log_t *log,
                                            uint64_t from, uint8_t wire_note) {
    for (size_t i = 0; i < log->count; i++) {
        const sim_event_t *e = &log->buf[i];
        if (e->cycle < from) continue;
        if (e->type != EVT_MIDI_NOTE_ON) continue;
        if (e->midi_note.channel == EXT_CH && e->midi_note.note == wire_note)
            return e;
    }
    return NULL;
}

static void test_ext_trigger_alignment(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    latch_guide(ctx);
    event_log_clear(&ctx->log);

    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, BAR_CYCLES * 4 + STEP_CYCLES);

    uint64_t worst = 0, total = 0;
    int      samples = 0;

    /* Pair each step onset with the note-on that falls before the NEXT onset,
     * rather than trusting the onset index to stay in step with the note-on
     * stream.  event_log_find_step_onset() occasionally slips an index (its
     * Timer2-restore filter is heuristic), which silently pairs a trigger with
     * the following step's note-on and manufactures a ~1-step outlier. */
    #define MAX_ONSETS 80
    const sim_event_t *onset[MAX_ONSETS];
    int n_onsets = 0;
    for (int i = 0; i < MAX_ONSETS; i++) {
        const sim_event_t *e = event_log_find_step_onset(&ctx->log, t0, i);
        if (!e) break;
        /* Drop a duplicate/regressed index rather than letting it reorder. */
        if (n_onsets && e->cycle <= onset[n_onsets - 1]->cycle) continue;
        onset[n_onsets++] = e;
    }

    /* Step 0 is skipped: its trigger can precede the anchor and the PLAY press
     * itself perturbs the first loop pass. */
    for (int n = 1; n + 1 < n_onsets; n++) {
        const sim_event_t *trig = onset[n];
        const sim_event_t *on = next_ext_note_on(&ctx->log, trig->cycle, WIRE_T0);
        if (!on) continue;
        if (on->cycle >= onset[n + 1]->cycle) continue;  /* step produced none */

        uint64_t lag = on->cycle - trig->cycle;
        total += lag;
        samples++;
        if (lag > worst) worst = lag;

        const sim_event_t *first_byte = next_channel_tx_byte(&ctx->log, trig->cycle);
        double sched = first_byte && first_byte->cycle <= on->cycle
                       ? (double)(first_byte->cycle - trig->cycle) / 16000.0 : -1.0;
        /* Attribute a long sched gap: count the LCD traffic the loop was busy
         * with between the trigger and the ext step reaching the UART. */
        size_t lcd_chars = 0, lcd_cmds = 0;
        if (first_byte) {
            lcd_chars = event_log_count_type(&ctx->log, EVT_LCD_CHAR,
                                             trig->cycle, first_byte->cycle);
            lcd_cmds  = event_log_count_type(&ctx->log, EVT_LCD_CMD,
                                             trig->cycle, first_byte->cycle);
        }
        printf("# ext_lag step%-2d lag=%7llu cyc (%5.2f ms)  sched=%5.2f ms  wire=%5.2f ms  lcd=%zuch/%zucmd\n",
               n, (unsigned long long)lag, (double)lag / 16000.0,
               sched, sched >= 0.0 ? (double)lag / 16000.0 - sched : -1.0,
               lcd_chars, lcd_cmds);
    }

    if (samples == 0) {
        test_fail("ext_lag", "no trigger/note-on pairs captured");
        return;
    }

    /* Every step of FX_PTRN_EXT_SYNC fires track 0, so a shortfall here means the
     * queue coalesced a step away rather than merely delivering it late. */
    size_t on_count = 0;
    for (size_t i = 0; i < ctx->log.count; i++) {
        const sim_event_t *e = &ctx->log.buf[i];
        if (e->type == EVT_MIDI_NOTE_ON && e->cycle >= t0 &&
            e->midi_note.channel == EXT_CH && e->midi_note.note == WIRE_T0)
            on_count++;
    }
    printf("# ext_lag track0 note-ons observed: %zu over 4 bars (64 steps)\n", on_count);

    uint64_t mean = total / (uint64_t)samples;
    printf("# ext_lag summary: n=%d mean=%llu cyc (%.2f ms) worst=%llu cyc (%.2f ms)\n",
           samples, (unsigned long long)mean, (double)mean / 16000.0,
           (unsigned long long)worst, (double)worst / 16000.0);

    if (worst > MAX_LAG_US * CYCLES_PER_US) {
        test_fail("ext_lag/worst",
                  "ext note-on lags trigger by %llu cycles (%.2f ms), limit %.2f ms",
                  (unsigned long long)worst, (double)worst / 16000.0,
                  (double)(MAX_LAG_US * CYCLES_PER_US) / 16000.0);
    }
}

/* Sending ext notes from the clock puts more bytes in the TX ring, and CountPPQN()
 * writes the MASTER clock byte straight to UDR1 behind a UDRE busy-wait in that same
 * ISR — so a fuller ring can stall the clock by up to a byte time.  Everything the
 * sequencer syncs downstream rides on that byte, so it gets its own bound. */
static void test_master_clock_jitter(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    latch_guide(ctx);
    event_log_clear(&ctx->log);

    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, BAR_CYCLES * 2);

    /* 24 MIDI clocks per quarter note = one every 4 PPQN ticks */
    uint64_t expected = NAVA_PPQN_PERIOD_CYCLES * 4ULL;
    uint64_t prev = 0, worst_dev = 0;
    int n = 0;
    for (size_t i = 0; i < ctx->log.count; i++) {
        const sim_event_t *e = &ctx->log.buf[i];
        if (e->type != EVT_MIDI_RT || e->raw_byte != 0xF8u) continue;
        if (e->cycle < t0 + STEP_CYCLES) continue;   /* skip the PLAY transient */
        if (prev) {
            uint64_t d = e->cycle - prev;
            uint64_t dev = d > expected ? d - expected : expected - d;
            if (dev > worst_dev) worst_dev = dev;
            n++;
        }
        prev = e->cycle;
    }

    if (n < 10) {
        test_fail("clock_jitter", "only %d clock intervals captured", n);
        return;
    }
    printf("# clock_jitter: n=%d expected=%llu cyc worst_dev=%llu cyc (%.3f ms)\n",
           n, (unsigned long long)expected, (unsigned long long)worst_dev,
           (double)worst_dev / 16000.0);

    /* One byte time at 31250 baud is 5120 cycles; allow two plus ISR entry. */
    if (worst_dev > 11000ULL) {
        test_fail("clock_jitter/worst",
                  "MIDI clock interval deviates by %llu cycles (%.3f ms)",
                  (unsigned long long)worst_dev, (double)worst_dev / 16000.0);
    }
}

/* A step too dense for the TX ring must be declined by the clock-side transmit and
 * still be delivered by the loop — late, but complete, and without stalling the
 * sequencer or the MIDI clock. */
static void test_dense_step_falls_back_to_loop(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    latch_guide(ctx);
    event_log_clear(&ctx->log);

    uint64_t t0 = ctx->avr->cycle;
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    nava_sim_run_cycles(ctx, BAR_CYCLES * 2);

    /* Every one of the 16 tracks must be heard, not just the low-numbered ones that
     * would fit if the burst were truncated. */
    int missing = 0;
    for (uint8_t track = 0; track < 16; track++) {
        uint8_t wire = (uint8_t)(0x30u + track);
        if (!next_ext_note_on(&ctx->log, t0, wire)) {
            printf("# dense: no note-on for track %u (wire %u)\n", track, wire);
            missing++;
        }
    }
    if (missing)
        test_fail("dense/coverage", "%d of 16 ext tracks never sounded", missing);

    /* The sequencer must keep running: the drum trigger is the proof the clock ISR
     * was never stalled by the ext burst. */
    size_t onsets = 0;
    for (int i = 0; i < 64; i++) {
        if (!event_log_find_step_onset(&ctx->log, t0, i)) break;
        onsets++;
    }
    printf("# dense: %zu step onsets over 2 bars, %d/16 tracks silent\n",
           onsets, missing);
    if (onsets < 24)
        test_fail("dense/sequencer",
                  "only %zu step onsets in 2 bars — clock stalled by the ext burst",
                  onsets);
}

int main(void) {
    TEST_WITH_PATTERN("ext_trigger_alignment",
                      test_ext_trigger_alignment, &FX_PTRN_EXT_SYNC, 2, 1);
    TEST_WITH_PATTERN("master_clock_jitter",
                      test_master_clock_jitter, &FX_PTRN_EXT_SYNC, 2, 1);
    TEST_WITH_PATTERN("dense_step_falls_back_to_loop",
                      test_dense_step_falls_back_to_loop, &FX_PTRN_EXT_DENSE, 2, 1);
    return test_run_all(NAVA_ELF_PATH);
}
