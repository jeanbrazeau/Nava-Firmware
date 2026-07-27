/* sim/harness/frontpanel.c
 * CI-060 fp_press_button  — set 74HC165 parallel-input bit + hold >=2 scans
 * CI-061 fp_release_button — clear bit after guaranteed hold
 * CI-062 observable accessors — last TRIG_WORD, LCD lines, MIDI notes, DIN edges
 *
 * Debounce period: ScanDin fires every DEBOUNCE_TIME=5ms plus once immediately.
 * Two reads must match (tempDin[a][0]==tempDin[a][1]).  We guarantee the button
 * state persists across at least 2 full scan periods = 10 ms simulated time
 * = 160000 cycles at 16 MHz. */
#include "frontpanel.h"
#include "lcd.h"
#include "midi.h"
#include "event_log.h"
#include "sim_avr.h"

#include <stddef.h>
#include <assert.h>

/* Minimum cycles to hold a button pressed so both debounce reads agree.
 * 2 × 5 ms periods + margin = 200000 cycles. */
#define HOLD_CYCLES_MIN  200000ULL

/* Two debounce periods to settle after release */
#define SETTLE_CYCLES    320000ULL

/* ---- Button bit mapping ----
 * Each entry: {panel_latch byte index, bitmask within that byte}
 * Matches firmware define.h BTN_* constants applied to dinSr[2], [3], [4]. */
typedef struct { int byte_idx; uint8_t mask; } btn_loc_t;

static const btn_loc_t BTN_LOC[] = {
    /* FP_BTN_PLAY      */ { 2, 0x80u },  /* BTN_PLAY  B10000000 */
    /* FP_BTN_STOP      */ { 2, 0x01u },  /* BTN_STOP  B00000001 */
    /* FP_BTN_CLEAR     */ { 2, 0x04u },  /* BTN_CLEAR B00000100 */
    /* FP_BTN_SHIFT     */ { 2, 0x08u },  /* BTN_SHIFT B00001000 */
    /* FP_BTN_INST      */ { 2, 0x10u },  /* BTN_INST  B00010000 */
    /* FP_BTN_SCALE     */ { 2, 0x02u },  /* BTN_SCALE B00000010 */
    /* FP_BTN_LASTSTEP  */ { 2, 0x40u },  /* BTN_LASTSTEP B01000000 */
    /* FP_BTN_SHUF      */ { 2, 0x20u },  /* BTN_SHUF  B00100000 */
    /* FP_BTN_TRK       */ { 3, 0x08u },  /* BTN_TRK   B00001000 */
    /* FP_BTN_PTRN      */ { 3, 0x80u },  /* BTN_PTRN  B10000000 */
    /* FP_BTN_TAP       */ { 3, 0x40u },  /* BTN_TAP   B01000000 */
    /* FP_BTN_DIR       */ { 3, 0x20u },  /* BTN_DIR   B00100000 */
    /* FP_BTN_GUIDE     */ { 3, 0x10u },  /* BTN_GUIDE B00010000 */
    /* FP_BTN_FWD       */ { 3, 0x02u },  /* BTN_FWD   B00000010 */
    /* FP_BTN_BACK      */ { 3, 0x04u },  /* BTN_BACK  B00000100 */
    /* FP_BTN_NUM       */ { 3, 0x01u },  /* BTN_NUM   B00000001 */
    /* FP_BTN_BANK      */ { 4, 0x01u },  /* BTN_BANK  B00000001 */
    /* FP_BTN_MUTE      */ { 4, 0x02u },  /* BTN_MUTE  B00000010 */
    /* FP_BTN_TEMPO     */ { 4, 0x04u },  /* BTN_TEMPO B00000100 */
    /* FP_BTN_ENTER     */ { 4, 0x08u },  /* BTN_ENTER B00001000 */
};

static void advance_cycles(nava_sim_t *ctx, uint64_t n) {
    struct avr_t *avr = ctx->avr;
    uint64_t target = avr->cycle + n;
    while (avr->cycle < target) avr_run(avr);
}

/* CI-060 */
void fp_press_button(nava_sim_t *ctx, fp_button_t btn) {
    assert(btn >= 0 && btn < (fp_button_t)(sizeof(BTN_LOC)/sizeof(BTN_LOC[0])));
    const btn_loc_t *loc = &BTN_LOC[btn];
    ctx->panel_latch[loc->byte_idx] |= loc->mask;
    /* Hold across >=2 debounce scan periods so both reads agree */
    advance_cycles(ctx, HOLD_CYCLES_MIN);
}

/* CI-061 */
void fp_release_button(nava_sim_t *ctx, fp_button_t btn) {
    assert(btn >= 0 && btn < (fp_button_t)(sizeof(BTN_LOC)/sizeof(BTN_LOC[0])));
    const btn_loc_t *loc = &BTN_LOC[btn];
    ctx->panel_latch[loc->byte_idx] &= ~loc->mask;
    /* Let the firmware sample the release across two scans */
    advance_cycles(ctx, HOLD_CYCLES_MIN);
}

/* Step buttons are in dinSr[0..1] packed LSB-first */
void fp_press_step(nava_sim_t *ctx, int step_0based) {
    assert(step_0based >= 0 && step_0based < 16);
    int byte_idx = step_0based / 8;           /* 0 or 1 */
    int bit_idx  = step_0based % 8;
    ctx->panel_latch[byte_idx] |= (uint8_t)(1u << bit_idx);
    advance_cycles(ctx, HOLD_CYCLES_MIN);
}

void fp_release_step(nava_sim_t *ctx, int step_0based) {
    assert(step_0based >= 0 && step_0based < 16);
    int byte_idx = step_0based / 8;
    int bit_idx  = step_0based % 8;
    ctx->panel_latch[byte_idx] &= ~(uint8_t)(1u << bit_idx);
    advance_cycles(ctx, HOLD_CYCLES_MIN);
}

void fp_settle(nava_sim_t *ctx) {
    advance_cycles(ctx, SETTLE_CYCLES);
}

/* ---- CI-062: observable accessors ---- */

const sim_event_t *fp_last_trigger_word(const nava_sim_t *ctx) {
    /* Scan backwards for the most recent TRIG_WORD */
    const event_log_t *log = &ctx->log;
    for (size_t i = log->count; i-- > 0; ) {
        if (log->buf[i].type == EVT_TRIG_WORD) return &log->buf[i];
    }
    return NULL;
}

uint16_t fp_last_trig_word_value(const nava_sim_t *ctx) {
    const sim_event_t *e = fp_last_trigger_word(ctx);
    return e ? e->trig_word : 0u;
}

const char *fp_lcd_line(const nava_sim_t *ctx, int row) {
    return nava_lcd_get_line(ctx->lcd, row);
}

/* ---- LED decoding ---- */

static const sim_event_t *last_led_write(const nava_sim_t *ctx) {
    const event_log_t *log = &ctx->log;
    for (size_t i = log->count; i-- > 0; )
        if (log->buf[i].type == EVT_LED_WRITE) return &log->buf[i];
    return NULL;
}

uint16_t fp_step_leds(const nava_sim_t *ctx) {
    const sim_event_t *e = last_led_write(ctx);
    return e ? (uint16_t)((e->led.bytes[0] << 8) | e->led.bytes[1]) : 0u;
}

uint16_t fp_config_leds(const nava_sim_t *ctx) {
    const sim_event_t *e = last_led_write(ctx);
    return e ? (uint16_t)((e->led.bytes[2] << 8) | e->led.bytes[3]) : 0u;
}

uint8_t fp_menu_leds(const nava_sim_t *ctx) {
    const sim_event_t *e = last_led_write(ctx);
    return e ? e->led.bytes[4] : 0u;
}

/* Bit position of each named LED, and which word it lives in.
 * menuLed  (Led.ino:18,22): stop0 shift1 inst2 clear3 shuf4 laststep5 scale6 play7
 * configLed(Led.ino:73)   : num0 scale1-4 track5 back6 fwd7 enter8 ptrn9 tap10
 *                           dir11 guide12 bank13 mute14 tempo15 */
static const struct { const char *name; uint8_t bit; uint8_t is_config; }
LED_MAP[] = {
    { "STOP",     0, 0 }, { "SHIFT",    1, 0 }, { "INST",  2, 0 },
    { "CLEAR",    3, 0 }, { "SHUF",     4, 0 }, { "LASTSTEP", 5, 0 },
    { "SCALE",    6, 0 }, { "PLAY",     7, 0 },
    { "NUM",      0, 1 }, { "TRACK",    5, 1 }, { "BACK",  6, 1 },
    { "FWD",      7, 1 }, { "ENTER",    8, 1 }, { "PTRN",  9, 1 },
    { "TAP",     10, 1 }, { "DIR",     11, 1 }, { "GUIDE", 12, 1 },
    { "BANK",    13, 1 }, { "MUTE",    14, 1 }, { "TEMPO", 15, 1 },
};

bool fp_led_on(const nava_sim_t *ctx, fp_led_t led) {
    if (led < 0 || led >= (fp_led_t)(sizeof(LED_MAP)/sizeof(LED_MAP[0])))
        return false;
    uint32_t word = LED_MAP[led].is_config ? fp_config_leds(ctx)
                                           : fp_menu_leds(ctx);
    return (word >> LED_MAP[led].bit) & 1u;
}

const char *fp_led_name(fp_led_t led) {
    if (led < 0 || led >= (fp_led_t)(sizeof(LED_MAP)/sizeof(LED_MAP[0])))
        return "?";
    return LED_MAP[led].name;
}

const sim_event_t *fp_next_midi_note_on(const nava_sim_t *ctx,
                                          uint8_t channel, uint8_t wire_note,
                                          uint64_t cycle_lo, uint64_t cycle_hi) {
    return nava_midi_expect_note_on(&ctx->log, channel, wire_note,
                                    cycle_lo, cycle_hi);
}

size_t fp_din_clock_edges(const nava_sim_t *ctx,
                           uint64_t cycle_lo, uint64_t cycle_hi) {
    return event_log_count_type(&ctx->log, EVT_DIN_CLK, cycle_lo, cycle_hi);
}
