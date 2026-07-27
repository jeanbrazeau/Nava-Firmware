/* sim/tests/test_ui.c
 * Front-panel UI regression tests: boot screen, mode transitions, mode/menu
 * LEDs, and the tempo readout tracking the encoder.
 *
 * Timing rules this suite depends on (learned the hard way — see README
 * "Test Harness Gotchas"):
 *
 *   1. Leave an IDLE GAP before each press.  A button pressed immediately
 *      after the previous release is swallowed by the firmware's debounce
 *      state machine and the mode change silently never happens.
 *   2. POLL for the expected screen, never read once after a fixed delay.
 *      Redraws are slow and non-atomic: a single read can catch a half-updated
 *      screen with old and new text mixed (e.g. " Track Plcl ins ").  Mode
 *      changes also vary hugely in cost — TRK lands in ~0.5M cycles, while
 *      PTRN takes ~12M because it reloads a pattern over the I2C bus.
 *   3. TEMPO is MOMENTARY: it shows the tempo only while held, and reverts on
 *      release.  Read it with the button still down.
 */
#include "test_runner.h"
#include "frontpanel.h"
#include "gpio.h"
#include "nava_sim.h"
#include "patterns.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOOT_CYCLES     64000000ULL
/* Quiet period before a press so the previous release is fully debounced. */
#define IDLE_GAP        2000000ULL
/* Generous: the PTRN transition reloads a pattern over I2C (~12M observed). */
#define UI_TIMEOUT      40000000ULL
#define POLL_STEP        500000ULL

/* Run until LCD `row` contains `want`, or the budget runs out. */
static bool lcd_await(nava_sim_t *ctx, int row, const char *want,
                      uint64_t max_cycles) {
    uint64_t spent = 0;
    while (spent < max_cycles) {
        const char *line = fp_lcd_line(ctx, row);
        if (line && strstr(line, want)) return true;
        nava_sim_run_cycles(ctx, POLL_STEP);
        spent += POLL_STEP;
    }
    const char *line = fp_lcd_line(ctx, row);
    return line && strstr(line, want);
}

/* Idle, tap a button, then wait for the screen it should produce. */
static bool tap_await(nava_sim_t *ctx, fp_button_t btn, const char *label,
                      int row, const char *want) {
    nava_sim_run_cycles(ctx, IDLE_GAP);
    fp_press_button(ctx, btn);
    fp_release_button(ctx, btn);
    if (!lcd_await(ctx, row, want, UI_TIMEOUT)) {
        test_fail(label, "LCD row %d never showed \"%s\" (last: \"%s\")",
                  row, want, fp_lcd_line(ctx, row));
        return false;
    }
    return true;
}

/* Run until `led` reaches `want_on`, or the budget runs out.
 * LED writes and LCD redraws are on independent cadences, so a mode LED can
 * still be stale at the moment its new screen first appears. */
static bool led_await(nava_sim_t *ctx, fp_led_t led, bool want_on,
                      uint64_t max_cycles) {
    uint64_t spent = 0;
    while (spent < max_cycles) {
        if (fp_led_on(ctx, led) == want_on) return true;
        nava_sim_run_cycles(ctx, POLL_STEP);
        spent += POLL_STEP;
    }
    return fp_led_on(ctx, led) == want_on;
}

static void expect_led(nava_sim_t *ctx, const char *label,
                       fp_led_t led, bool want_on) {
    if (!led_await(ctx, led, want_on, UI_TIMEOUT))
        test_fail(label, "LED %s: expected %s (cfg=%04X menu=%02X)",
                  fp_led_name(led), want_on ? "ON" : "OFF",
                  fp_config_leds(ctx), fp_menu_leds(ctx));
}

/* Last unsigned integer on a line — the tempo readout renders as "...-120". */
static int trailing_number(const char *s) {
    int val = -1;
    for (const char *p = s; *p; p++)
        if (*p >= '0' && *p <= '9') { val = atoi(p); while (p[1] >= '0' && p[1] <= '9') p++; }
    return val;
}

/* ---- Tests ---- */

static void test_boot_screen(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    nava_sim_run_cycles(ctx, IDLE_GAP);

    /* Pattern-step edit is the default boot screen for this EEPROM config. */
    assert_lcd_contains("ui/boot", ctx, 0, "ptr");
    assert_lcd_contains("ui/boot", ctx, 0, "scl");

    /* Stopped at boot.  Assert STOP, never PLAY: Led.ino:22 composes the
     * stopped state as (LED_PLAY * blinkTempo) | LED_STOP, so PLAY *blinks*
     * while stopped and reads either way depending on when you sample.
     * STOP is the steady indicator. */
    expect_led(ctx, "ui/boot", FP_LED_STOP, true);
}

static void test_mode_transitions(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);

    if (tap_await(ctx, FP_BTN_TRK, "ui/mode/track", 0, "Track"))
        expect_led(ctx, "ui/mode/track", FP_LED_TRACK, true);

    if (tap_await(ctx, FP_BTN_PTRN, "ui/mode/pattern", 0, "Pattern")) {
        expect_led(ctx, "ui/mode/pattern", FP_LED_PTRN, true);
        /* Leaving track mode must clear its indicator. */
        expect_led(ctx, "ui/mode/pattern", FP_LED_TRACK, false);
    }
}

static void test_play_stop_leds(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    nava_sim_run_cycles(ctx, IDLE_GAP);

    /* Running vs stopped is read off the STOP lamp, not PLAY:
     *   running (Led.ino:18) -> menuLed = LED_PLAY & ~LED_STOP  (STOP clear)
     *   stopped (Led.ino:22) -> menuLed = ... | LED_STOP        (STOP set)
     * PLAY blinks while stopped, so it cannot distinguish the two states. */
    fp_press_button(ctx, FP_BTN_PLAY);
    fp_release_button(ctx, FP_BTN_PLAY);
    expect_led(ctx, "ui/play", FP_LED_STOP, false);   /* STOP clears when running */

    nava_sim_run_cycles(ctx, IDLE_GAP);
    fp_press_button(ctx, FP_BTN_STOP);
    fp_release_button(ctx, FP_BTN_STOP);
    expect_led(ctx, "ui/stop", FP_LED_STOP, true);
}

static void test_tempo_tracks_encoder(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    nava_sim_run_cycles(ctx, IDLE_GAP);

    /* TEMPO is momentary — hold it for the whole measurement. */
    fp_press_button(ctx, FP_BTN_TEMPO);
    nava_sim_run_cycles(ctx, 2000000ULL);

    int base = trailing_number(fp_lcd_line(ctx, 1));
    if (base < 0) {
        test_fail("ui/tempo", "no tempo number while TEMPO held (row1=\"%s\")",
                  fp_lcd_line(ctx, 1));
        fp_release_button(ctx, FP_BTN_TEMPO);
        return;
    }
    /* Seeded EEPROM sets defaultBpm=120. */
    if (base != 120)
        test_fail("ui/tempo", "expected tempo 120 at boot, got %d", base);

    nava_gpio_inject_encoder(ctx->gpio, +1, 4);
    nava_sim_run_cycles(ctx, 2000000ULL);
    int up = trailing_number(fp_lcd_line(ctx, 1));
    if (up <= base)
        test_fail("ui/tempo", "clockwise encoder did not raise tempo: %d -> %d",
                  base, up);

    nava_gpio_inject_encoder(ctx->gpio, -1, 4);
    nava_sim_run_cycles(ctx, 2000000ULL);
    int down = trailing_number(fp_lcd_line(ctx, 1));
    if (down >= up)
        test_fail("ui/tempo", "counter-clockwise did not lower tempo: %d -> %d",
                  up, down);

    fp_release_button(ctx, FP_BTN_TEMPO);
    nava_sim_run_cycles(ctx, 2000000ULL);
    printf("# ui/tempo: %d -> %d (CW) -> %d (CCW)\n", base, up, down);
}

int main(void) {
    TEST_WITH_PATTERN("ui_boot_screen",
                      test_boot_screen, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ui_mode_transitions_track_pattern",
                      test_mode_transitions, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ui_play_stop_menu_leds",
                      test_play_stop_leds, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ui_tempo_display_tracks_encoder",
                      test_tempo_tracks_encoder, &FX_PTRN_BASIC, 2, 1);

    return test_run_all(NAVA_ELF_PATH);
}
