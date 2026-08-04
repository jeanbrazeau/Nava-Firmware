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

/* Boot budget: ~6 s simulated at 16 MHz. Boot itself measures ~4.2 s (67M
 * cycles): the panel dissolve animation is ~1.26 s of it, then the 2 s version
 * splash, and the panel is not scanned until both are done. */
#define BOOT_CYCLES     96000000ULL
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

/* Idle, tap a step button, then wait for the config page it should select. */
static bool step_await(nava_sim_t *ctx, int step_0based, const char *label,
                       const char *want) {
    nava_sim_run_cycles(ctx, IDLE_GAP);
    fp_press_step(ctx, step_0based);
    fp_release_step(ctx, step_0based);
    if (!lcd_await(ctx, 0, want, UI_TIMEOUT)) {
        test_fail(label, "LCD row 0 never showed \"%s\" (last: \"%s\")",
                  want, fp_lcd_line(ctx, 0));
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

/* The power-on animation dissolves the panel in and every cell ends as the ROM
 * block glyph 0xFF, so a fully lit screen is 16 bytes of 0xFF per row.
 *
 * Two things are asserted, and the second is the point of the design. A cell
 * part way up the fill ladder shows a custom glyph, code 0-7, so counting those
 * on a row counts the cells in motion at that instant. An implementation that
 * hands each cell its own glyph can never have more than EIGHT cells mid-fill
 * across the whole panel; the shared ladder puts nearly every cell in motion at
 * once, which is what makes it read as a dissolve rather than as clumps.
 *
 * Checked on row 1 only. This LCD model keeps CGRAM in the same array as DDRAM
 * (0x00-0x3F), which is where row 0 lives, so the ladder upload scribbles over
 * row 0's mirror; row 1 starts at 0x40 and no CGRAM write reaches it. Reading
 * raw bytes rather than the text mirror is what makes any of this assertable:
 * the mirror renders 0xFF and a half-filled glyph identically as '.'. */
#define BOOT_FILL_MIN_CONCURRENT 12   /* of 16 on the row; 8 is the per-cell-glyph ceiling */

static void test_boot_fill_animation(nava_sim_t *ctx) {
    bool filled = false;
    uint64_t spent = 0;
    int peak_partial = 0;
    while (spent < BOOT_CYCLES && !filled) {
        const uint8_t *raw = fp_lcd_raw(ctx, 1);
        int partial = 0;
        filled = true;
        for (int col = 0; col < 16; col++) {
            if (raw[col] != 0xFF) filled = false;
            if (raw[col] < 8) partial++;      /* custom glyph: cell is mid-fill */
        }
        if (partial > peak_partial) peak_partial = partial;
        if (!filled) {
            nava_sim_run_cycles(ctx, POLL_STEP);
            spent += POLL_STEP;
        }
    }
    if (peak_partial < BOOT_FILL_MIN_CONCURRENT)
        test_fail("ui/bootfill",
                  "only %d of 16 cells on row 1 were ever mid-fill at once "
                  "(want >= %d) — the fill is running in clumps, not dissolving",
                  peak_partial, BOOT_FILL_MIN_CONCURRENT);
    else
        printf("# ui/bootfill: %d of 16 cells mid-fill at the peak\n", peak_partial);
    if (!filled) {
        const uint8_t *raw = fp_lcd_raw(ctx, 1);
        test_fail("ui/bootfill",
                  "row 1 never reached all-0xFF (last: %02X %02X %02X ... %02X)",
                  raw[0], raw[1], raw[2], raw[15]);
        return;
    }

    /* The fill is a prelude, not the end state: the splash must follow it. */
    if (!lcd_await(ctx, 1, "solutions", BOOT_CYCLES)) {
        test_fail("ui/bootfill", "splash never replaced the fill (row 1: \"%s\")",
                  fp_lcd_line(ctx, 1));
        return;
    }

    /* The two words are one name stacked over two rows, so they must start in
     * the same column. Row 1 is indented by the leading space that keeps
     * " solutions " + version inside 16 columns; row 0 has to match it. */
    const char *row0 = strstr(fp_lcd_line(ctx, 0), "downtown");
    const char *row1 = strstr(fp_lcd_line(ctx, 1), "solutions");
    if (!row0 || row0 - fp_lcd_line(ctx, 0) != row1 - fp_lcd_line(ctx, 1))
        test_fail("ui/bootfill",
                  "splash words misaligned (\"%s\" / \"%s\")",
                  fp_lcd_line(ctx, 0), fp_lcd_line(ctx, 1));
}

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

/* Config mode lights step LEDs 1..MAX_CONF_PAGE to advertise the pages; those same
 * step buttons select the page directly, alongside the SHIFT+TEMPO cycle.  Page
 * markers are the row-0 headers, in the define.h order: 1 "bpm", 2 "mte",
 * 3 "ext vel", 4 "ype" (sysex), 5 "BOOTLOADER".  The first character of each header
 * is the blinking cursor letter, uppercased in place, so the markers deliberately
 * start past column 0. */
static void test_config_page_step_buttons(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    nava_sim_run_cycles(ctx, IDLE_GAP);

    char before[64];
    snprintf(before, sizeof before, "%s", fp_lcd_line(ctx, 1));

    /* SHIFT has to stay down across the tap: TEMPO alone leaves config mode. */
    nava_sim_run_cycles(ctx, IDLE_GAP);
    fp_press_button(ctx, FP_BTN_SHIFT);
    fp_press_button(ctx, FP_BTN_TEMPO);
    fp_release_button(ctx, FP_BTN_TEMPO);
    fp_release_button(ctx, FP_BTN_SHIFT);
    if (!lcd_await(ctx, 0, "bpm", UI_TIMEOUT)) {
        test_fail("ui/config/enter", "config page 1 never appeared (row0=\"%s\")",
                  fp_lcd_line(ctx, 0));
        return;
    }

    /* Jump around the pages out of cycle order - that is the whole point of the
     * binding.  The sysex page is included because selecting it enables SysEx mode,
     * which flushes the pattern bank; the step route has to reach that the same way
     * the TEMPO route does. */
    if (!step_await(ctx, 4, "ui/config/step5", "BOOTLOADER")) return;
    if (!step_await(ctx, 1, "ui/config/step2", "mte")) return;
    if (!step_await(ctx, 3, "ui/config/step4", "ype")) return;
    if (!step_await(ctx, 2, "ui/config/step3", "ext vel")) return;
    if (!step_await(ctx, 0, "ui/config/step1", "bpm")) return;

    /* Buttons past the last page are dark, and must not wrap onto a page the user
     * did not aim at. */
    nava_sim_run_cycles(ctx, IDLE_GAP);
    fp_press_step(ctx, 15);
    fp_release_step(ctx, 15);
    nava_sim_run_cycles(ctx, UI_TIMEOUT / 4);
    if (!strstr(fp_lcd_line(ctx, 0), "bpm"))
        test_fail("ui/config/step16", "step 16 moved off page 1 (row0=\"%s\")",
                  fp_lcd_line(ctx, 0));

    /* Leaving restores the edit screen.  Row 1 opens with bank, pattern and length,
     * and in PTRN_STEP a step press selects a pattern - so this is what catches the
     * page presses leaking through to the sequencer underneath.  Only that prefix is
     * compared: TEMPO is momentary and the tempo readout it paints into the tail of
     * the row outlives the release by a redraw. */
    nava_sim_run_cycles(ctx, IDLE_GAP);
    fp_press_button(ctx, FP_BTN_TEMPO);
    fp_release_button(ctx, FP_BTN_TEMPO);
    if (!lcd_await(ctx, 0, "ptr", UI_TIMEOUT)) {
        test_fail("ui/config/exit", "edit screen never returned (row0=\"%s\")",
                  fp_lcd_line(ctx, 0));
        return;
    }
    if (strncmp(fp_lcd_line(ctx, 1), before, 8) != 0)
        test_fail("ui/config/no_side_effect",
                  "config page presses changed bank/pattern/length: \"%.8s\" -> \"%.8s\"",
                  before, fp_lcd_line(ctx, 1));

    printf("# ui/config: pages selected by step buttons, row1 \"%.8s\" unchanged\n",
           before);
}

int main(void) {
    TEST_WITH_PATTERN("ui_boot_fill_animation",
                      test_boot_fill_animation, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ui_boot_screen",
                      test_boot_screen, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ui_mode_transitions_track_pattern",
                      test_mode_transitions, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ui_play_stop_menu_leds",
                      test_play_stop_leds, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ui_tempo_display_tracks_encoder",
                      test_tempo_tracks_encoder, &FX_PTRN_BASIC, 2, 1);
    TEST_WITH_PATTERN("ui_config_page_step_buttons",
                      test_config_page_step_buttons, &FX_PTRN_BASIC, 2, 1);

    return test_run_all(NAVA_ELF_PATH);
}
