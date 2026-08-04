/* sim/tests/test_eeprom_init.c
 * What the PLAY+STOP -> PLAY+ENTER EEPROM wipe actually leaves behind.
 *
 * The machine this was written for came back from a wipe with a pattern whose scale was
 * not a legal value. scale reaches CountPPQN() as the divisor of `ppqn % scale`, and
 * shuffle reaches it as `shuffle[pattern.shuffle - 1]`, so a record that carries 0 in
 * either byte does not produce a wrong pattern - it produces a machine that latches PLAY,
 * lights nothing and sounds nothing, while step programming keeps working because that is
 * gated on isRunning alone. Nothing in the suite looked at what a wipe writes.
 *
 * The gesture: hold PLAY+STOP through power-on (ScanDinBoot latches it once, before the
 * splash), then press PLAY+ENTER within BOOTLOADER_TIME to confirm.
 */
#include "test_runner.h"
#include "frontpanel.h"
#include "nava_sim.h"
#include "patterns.h"

#include <stdio.h>
#include <stdlib.h>

/* Panel latch positions, from define.h's BTN_ masks (see frontpanel.h). */
#define LATCH_PLAY_BYTE  2
#define LATCH_PLAY_BIT   7
#define LATCH_STOP_BYTE  2
#define LATCH_STOP_BIT   0
#define LATCH_ENTER_BYTE 4
#define LATCH_ENTER_BIT  3

/* Offsets inside a 448-byte pattern record: 32 bytes of trigger words, then the setup
 * fields. Same layout fx_pattern_to_eeprom writes. */
#define REC_LENGTH  32
#define REC_SCALE   33
#define REC_SHUFFLE 34
#define REC_FLAM    35

/* InitEEprom writes ~1150 pages at 5 ms apiece plus the I2C traffic itself. */
#define WIPE_BUDGET_CYCLES  700000000ULL
#define WIPE_POLL_CYCLES     20000000ULL

static void test_wipe_writes_pattern_defaults(nava_sim_t *ctx) {
    /* Hold PLAY+STOP from cycle 0: ScanDinBoot() samples the panel exactly once, a few
     * milliseconds into setup(), and never looks again. */
    nava_sim_panel_set_bit(ctx, LATCH_PLAY_BYTE, LATCH_PLAY_BIT, 1);
    nava_sim_panel_set_bit(ctx, LATCH_STOP_BYTE, LATCH_STOP_BIT, 1);
    nava_sim_run_cycles(ctx, 8000000ULL);   /* 0.5 s: past ScanDinBoot and the prompt */
    assert_lcd_contains("wipe/prompt", ctx, 0, "init EEprom");

    /* Confirm with PLAY+ENTER, which is what actually runs InitEEprom(). */
    nava_sim_panel_set_bit(ctx, LATCH_STOP_BYTE, LATCH_STOP_BIT, 0);
    nava_sim_panel_set_bit(ctx, LATCH_ENTER_BYTE, LATCH_ENTER_BIT, 1);

    /* Poll the last pattern rather than run a fixed budget: the wipe's duration is set by
     * 1150-odd page writes and would otherwise have to be guessed. */
    uint8_t probe = 0xFF;
    uint64_t spent = 0;
    while (spent < WIPE_BUDGET_CYCLES) {
        nava_sim_run_cycles(ctx, WIPE_POLL_CYCLES);
        spent += WIPE_POLL_CYCLES;
        nava_sim_read_eeprom(ctx, (FX_MAX_PTRN - 1) * FX_PTRN_SIZE + REC_SCALE, &probe, 1);
        if (probe != 0xFF) break;
    }
    if (probe == 0xFF) {
        test_fail("wipe/complete",
                  "last pattern still unwritten after %llu cycles",
                  (unsigned long long)spent);
        return;
    }
    printf("# wipe/complete: %llu cycles\n", (unsigned long long)spent);

    /* Every pattern must come out of a wipe playable. A record whose scale is 0 divides
     * by zero in the clock ISR; one whose shuffle is 0 indexes shuffle[-1]. */
    for (unsigned p = 0; p < FX_MAX_PTRN; p++) {
        uint8_t rec[4];
        nava_sim_read_eeprom(ctx, p * FX_PTRN_SIZE + REC_LENGTH, rec, 4);
        if (rec[0] != 15 || rec[1] != FX_SCALE_16 || rec[2] < 1 || rec[3] > 7) {
            test_fail("wipe/defaults",
                      "pattern %u: length=%u scale=%u shuffle=%u flam=%u "
                      "(want 15/%u/>=1/<=7)",
                      p, rec[0], rec[1], rec[2], rec[3], FX_SCALE_16);
            return;
        }
    }
    printf("# wipe/defaults: all %u patterns carry length 15, scale %u, shuffle >= 1\n",
           FX_MAX_PTRN, FX_SCALE_16);
}

int main(void) {
    /* A blank part reads 0xFF, so anything the wipe does not write stays visible as
     * 0xFF rather than being masked by a fixture's own defaults. */
    uint8_t *blank = malloc(FX_EEPROM_SIZE);
    for (size_t i = 0; i < FX_EEPROM_SIZE; i++) blank[i] = 0xFF;

    static test_entry_t entry = {
        .name = "eeprom_wipe_writes_playable_pattern_defaults",
        .fn = test_wipe_writes_pattern_defaults,
        .ext_channel = 2, .ptrn_change_sync = 1,
    };
    entry.eeprom_image = blank;
    entry.eeprom_size = FX_EEPROM_SIZE;
    test_register(&entry);

    int rc = test_run_all(NAVA_ELF_PATH);
    free(blank);
    return rc;
}
