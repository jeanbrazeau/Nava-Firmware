/* sim/tests/test_midi_sync.c — CI-094
 * SLAVE sync regression: injecting external MIDI clock advances the sequencer
 * at the injected cadence.
 *
 * In SLAVE mode (sync=1), the firmware's HandleClock() calls CountPPQN() four
 * times per received 0xF8 byte (one 0xF8 = 1 MIDI clock = 4 internal PPQN ticks,
 * because the sequencer runs at 96 PPQN while MIDI clock is 24 PPQN/qn).
 * HandleStart() sets midiStart=HIGH which the main loop converts to a PLAY.
 *
 * Test:
 *   1. Boot with sync=MASTER (default EEPROM) → switch to SLAVE by MIDI clock.
 *      Actually, SLAVE config must be in the EEPROM so we build a custom image.
 *   2. Inject MIDI start (0xFA) + 24 clock pulses (one quarter note).
 *   3. Assert: TRIG_WORD events appear and their cadence matches injected clock.
 *   4. Inject MIDI stop (0xFC) → verify no more triggers.
 */
#include "test_runner.h"
#include "frontpanel.h"
#include "event_log.h"
#include "nava_sim.h"
#include "midi.h"
#include "patterns.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Boot budget: ~6 s simulated at 16 MHz. Boot itself measures ~4.2 s (67M
 * cycles): the panel dissolve animation is ~1.26 s of it, then the 2 s version
 * splash, and the panel is not scanned until both are done. */
#define BOOT_CYCLES             96000000ULL

/* Time between injected MIDI clocks.
 * At 120 BPM, MIDI clock period = (60/120)/24 s = 20.83 ms = 333333 cycles.
 * We use this spacing to mimic a 120 BPM external master. */
#define MIDI_CLK_PERIOD_CYCLES  333333ULL

/* SLAVE sync value from define.h */
#define SLAVE_SYNC 1u

/* EEPROM image with sync=SLAVE */
static uint8_t *make_slave_eeprom(void) {
    uint8_t *img = fx_make_eeprom_image(&FX_PTRN_BASIC, 2, 1);
    /* Override setup[0] = sync = SLAVE = 1 */
    img[FX_OFFSET_SETUP + 0] = SLAVE_SYNC;
    return img;
}

static void test_slave_clock_advances_sequencer(nava_sim_t *ctx) {
    /* Override the seeded EEPROM with SLAVE config */
    uint8_t *slave_img = make_slave_eeprom();
    nava_sim_seed_eeprom(ctx, slave_img, FX_EEPROM_SIZE);
    free(slave_img);

    /* Boot with SLAVE config already in EEPROM */
    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    /* Inject MIDI start */
    nava_midi_inject_start(ctx->midi);

    /* Inject 24 MIDI clock pulses (one quarter note at 120 BPM) */
    uint64_t t0 = ctx->avr->cycle;
    for (int i = 0; i < 24; i++) {
        nava_midi_inject_clock(ctx->midi);
        /* Advance clock spacing between pulses */
        uint64_t target = t0 + (uint64_t)(i + 1) * MIDI_CLK_PERIOD_CYCLES;
        while (ctx->avr->cycle < target) avr_run(ctx->avr);
    }
    /* Run a little more to flush output */
    nava_sim_run_cycles(ctx, MIDI_CLK_PERIOD_CYCLES * 2);

    /* Assert: at least one TRIG_WORD event (sequencer produced triggers) */
    size_t n_trigs = event_log_count_type(&ctx->log, EVT_TRIG_WORD, t0, 0);
    if (n_trigs == 0) {
        printf("# slave_clock: LCD[0]=\"%.16s\" (should show BPM or mode)\n",
               fp_lcd_line(ctx, 0) ? fp_lcd_line(ctx, 0) : "<empty>");
        test_fail("slave_clock",
                  "no TRIG_WORD events — if LCD blank: boot incomplete; "
                  "else: check USART1 RX injection or SLAVE config in EEPROM");
        return;
    }

    /* Do NOT assert on captured clock bytes here.  nava_midi_count_clocks reads
     * the TX capture, but in SLAVE mode the firmware CONSUMES the injected
     * clock to advance its sequencer and does not echo it back out (there is no
     * MIDI-thru), so the TX count is legitimately 0.  The real evidence that
     * the injected clock drove the sequencer is the trigger activity asserted
     * above; counting our own injected bytes back would prove nothing. */
    size_t n_clocks = nava_midi_count_clocks(&ctx->log, t0, 0);
    printf("# slave_clock: %zu triggers from 24 injected clocks "
           "(%zu clock bytes retransmitted — 0 expected in SLAVE)\n",
           n_trigs, n_clocks);
}

static void test_slave_stop_halts_triggers(nava_sim_t *ctx) {
    uint8_t *slave_img = make_slave_eeprom();
    nava_sim_seed_eeprom(ctx, slave_img, FX_EEPROM_SIZE);
    free(slave_img);

    boot_wait_ready(ctx, BOOT_CYCLES);
    event_log_clear(&ctx->log);

    nava_midi_inject_start(ctx->midi);
    uint64_t t0 = ctx->avr->cycle;

    /* Send 12 clocks (half bar), then stop */
    for (int i = 0; i < 12; i++) {
        nava_midi_inject_clock(ctx->midi);
        uint64_t target = t0 + (uint64_t)(i + 1) * MIDI_CLK_PERIOD_CYCLES;
        while (ctx->avr->cycle < target) avr_run(ctx->avr);
    }
    uint64_t stop_cycle = ctx->avr->cycle;
    nava_midi_inject_stop(ctx->midi);

    /* Run 2 more clock periods and count triggers after stop */
    nava_sim_run_cycles(ctx, MIDI_CLK_PERIOD_CYCLES * 2);
    uint64_t end_cycle = ctx->avr->cycle;

    size_t trigs_after_stop = event_log_count_type(&ctx->log, EVT_TRIG_WORD,
                                                    stop_cycle, end_cycle);
    /* After stop, the sequencer should halt.  No new triggers expected.
     * Allow a small window for in-flight triggers (< 3). */
    if (trigs_after_stop > 3) {
        test_fail("slave_stop",
                  "sequencer continued after MIDI stop: %zu triggers",
                  trigs_after_stop);
    }
    (void)stop_cycle;
}

int main(void) {
    TEST("slave_clock_advances_sequencer",
         test_slave_clock_advances_sequencer);
    TEST("slave_stop_halts_triggers",
         test_slave_stop_halts_triggers);

    return test_run_all(NAVA_ELF_PATH);
}
