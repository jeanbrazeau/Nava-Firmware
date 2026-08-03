/* sim/harness/frontpanel.h
 * CI-060..CI-062: Virtual front-panel API — named button press/release with
 * debounce-persistence guarantee, plus decoded observable accessors. */
#ifndef NAVA_FRONTPANEL_H
#define NAVA_FRONTPANEL_H

#include <stdint.h>
#include <stddef.h>
#include "nava_sim.h"
#include "event_log.h"

/* ---- Button name constants ----
 * These map symbolic names to (byte_index, bit_index) pairs in panel_latch[].
 * dinSr[0..1] = step buttons 1-16 (16-bit word, LSB = step 1)
 * dinSr[2]    = BTN_PLAY/STOP/CLEAR/INST/SHIFT/SCALE/LASTSTEP/SHUF
 * dinSr[3]    = BTN_TRK/PTRN/TAP/DIR/GUIDE/FWD/BACK/NUM
 * dinSr[4]    = BTN_BANK/MUTE/TEMPO/ENTER (plus upper nibble unused)
 */

/* Step buttons 1-16 occupy dinSr[1]:dinSr[0] as a 16-bit word.
 * step_button(n) is 0-based (n=0 = step 1). */
void fp_press_step(nava_sim_t *ctx, int step_0based);
void fp_release_step(nava_sim_t *ctx, int step_0based);

/* Function buttons (define.h BTN_* masks) */
typedef enum {
    FP_BTN_PLAY,    /* BTN_PLAY  = B10000000 in dinSr[2] bit 7 */
    FP_BTN_STOP,    /* BTN_STOP  = B00000001 in dinSr[2] bit 0 */
    FP_BTN_CLEAR,   /* BTN_CLEAR = B00000100 in dinSr[2] bit 2 */
    FP_BTN_SHIFT,   /* BTN_SHIFT = B00001000 in dinSr[2] bit 3 */
    FP_BTN_INST,    /* BTN_INST  = B00010000 in dinSr[2] bit 4 */
    FP_BTN_SCALE,   /* BTN_SCALE = B00000010 in dinSr[2] bit 1 */
    FP_BTN_LASTSTEP,/* BTN_LASTSTEP = B01000000 in dinSr[2] bit 6 */
    FP_BTN_SHUF,    /* BTN_SHUF  = B00100000 in dinSr[2] bit 5 */
    FP_BTN_TRK,     /* BTN_TRK   = B00001000 in dinSr[3] bit 3 */
    FP_BTN_PTRN,    /* BTN_PTRN  = B10000000 in dinSr[3] bit 7 */
    FP_BTN_TAP,     /* BTN_TAP   = B01000000 in dinSr[3] bit 6 */
    FP_BTN_DIR,     /* BTN_DIR   = B00100000 in dinSr[3] bit 5 */
    FP_BTN_GUIDE,   /* BTN_GUIDE = B00010000 in dinSr[3] bit 4 */
    FP_BTN_FWD,     /* BTN_FWD   = B00000010 in dinSr[3] bit 1 */
    FP_BTN_BACK,    /* BTN_BACK  = B00000100 in dinSr[3] bit 2 */
    FP_BTN_NUM,     /* BTN_NUM   = B00000001 in dinSr[3] bit 0 */
    FP_BTN_BANK,    /* BTN_BANK  = B00000001 in dinSr[4] bit 0 */
    FP_BTN_MUTE,    /* BTN_MUTE  = B00000010 in dinSr[4] bit 1 */
    FP_BTN_TEMPO,   /* BTN_TEMPO = B00000100 in dinSr[4] bit 2 */
    FP_BTN_ENTER,   /* BTN_ENTER = B00001000 in dinSr[4] bit 3 */
} fp_button_t;

/* Press/release a named button.
 * press_button holds the bit across at least 2 debounce scan periods
 * (~10 ms simulated = ~160000 cycles at 16 MHz) before returning.
 * Pressing fewer than 2 scans is not supported — always use this API. */
void fp_press_button(nava_sim_t *ctx, fp_button_t btn);
void fp_release_button(nava_sim_t *ctx, fp_button_t btn);

/* Advance simulation to let pending button state propagate.
 * Equivalent to running ~2 debounce periods (~20 ms = ~320000 cycles). */
void fp_settle(nava_sim_t *ctx);

/* ---- Observable accessors ---- */

/* Return the most recent TRIG_WORD event in the log (or NULL) */
const sim_event_t *fp_last_trigger_word(const nava_sim_t *ctx);

/* Return the most recent TRIG_WORD value, or 0 if none recorded. */
uint16_t fp_last_trig_word_value(const nava_sim_t *ctx);

/* Return current LCD line 0 or 1 */
const char *fp_lcd_line(const nava_sim_t *ctx, int row);
/* Raw DDRAM bytes of a row - see nava_lcd_get_raw() for when row 0 lies. */
const uint8_t *fp_lcd_raw(const nava_sim_t *ctx, int row);

/* ---- LED decoding ----
 * SetDoutLed (Dio.ino) shifts out 5 bytes, MSB-first per word:
 *   [0]=stepLed>>8  [1]=stepLed  [2]=configLed>>8  [3]=configLed  [4]=menuLed
 * These accessors reassemble the words from the most recent LED_WRITE event.
 *
 * CAUTION: many LEDs blink (Led.ino toggles them with blinkFast/blinkTempo),
 * so their state depends on WHEN you sample.  Assert on steady indicators —
 * the mode LEDs and PLAY/STOP — rather than on blinking step LEDs. */
uint16_t fp_step_leds(const nava_sim_t *ctx);    /* bit N = step N (0-based) */
uint16_t fp_config_leds(const nava_sim_t *ctx);
uint8_t  fp_menu_leds(const nava_sim_t *ctx);

/* Named LEDs, as composed in Led.ino:18/22 (menu) and Led.ino:73 (config). */
typedef enum {
    /* menuLed byte */
    FP_LED_STOP, FP_LED_SHIFT, FP_LED_INST, FP_LED_CLEAR,
    FP_LED_SHUF, FP_LED_LASTSTEP, FP_LED_SCALE, FP_LED_PLAY,
    /* configLed word */
    FP_LED_NUM, FP_LED_TRACK, FP_LED_BACK, FP_LED_FWD, FP_LED_ENTER,
    FP_LED_PTRN, FP_LED_TAP, FP_LED_DIR, FP_LED_GUIDE, FP_LED_BANK,
    FP_LED_MUTE, FP_LED_TEMPO,
} fp_led_t;

/* True if the named LED was lit in the most recent LED_WRITE. */
bool fp_led_on(const nava_sim_t *ctx, fp_led_t led);

/* Human-readable name, for diagnostics. */
const char *fp_led_name(fp_led_t led);

/* Find next MIDI note-on matching the given wire note (0=any) and channel (0=any) */
const sim_event_t *fp_next_midi_note_on(const nava_sim_t *ctx,
                                          uint8_t channel, uint8_t wire_note,
                                          uint64_t cycle_lo, uint64_t cycle_hi);

/* Count DIN_CLK edges in a cycle window */
size_t fp_din_clock_edges(const nava_sim_t *ctx,
                           uint64_t cycle_lo, uint64_t cycle_hi);

#endif /* NAVA_FRONTPANEL_H */
