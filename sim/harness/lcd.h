/* sim/harness/lcd.h
 * CI-040/CI-041: HD44780 LCD model in 4-bit write-only mode.
 * Firmware pins: RS=PC2, EN=PC3, D4-7=PC4-7.  No RW pin connected. */
#ifndef NAVA_LCD_H
#define NAVA_LCD_H

#include <stddef.h>
#include <stdint.h>
#include "event_log.h"

struct avr_t;
typedef struct nava_lcd_ctx nava_lcd_ctx_t;

/* Attach the HD44780 part to the AVR's PORTC pins.
 * The part runs 4-bit, write-only, 16×2 configuration. */
nava_lcd_ctx_t *nava_lcd_attach(struct avr_t *avr, event_log_t *log);
void nava_lcd_detach(nava_lcd_ctx_t *ctx);

/* Return current rendered text for row 0 or 1 (16 chars, NUL-terminated).
 * Returns NULL if the LCD has not produced output yet. */
const char *nava_lcd_get_line(const nava_lcd_ctx_t *ctx, int row);

/* Return the 16 raw DDRAM bytes of a row, unsubstituted.
 * nava_lcd_get_line() renders every non-ASCII code as '.', which cannot tell the
 * all-dots-on block (0xFF) from a partly filled custom glyph (codes 0-7) - a
 * distinction the boot fill animation is entirely made of.
 *
 * Row 0 is unreliable while custom glyphs are being written: the part keeps
 * CGRAM in the same vram array as DDRAM, at 0x00-0x3F, which is exactly where
 * row 0 lives. Row 1 starts at 0x40 and no CGRAM write can reach it. */
const uint8_t *nava_lcd_get_raw(const nava_lcd_ctx_t *ctx, int row);

/* Return 1 if the HD44780 part has received at least one DDRAM write. */
int nava_lcd_is_active(const nava_lcd_ctx_t *ctx);

#endif /* NAVA_LCD_H */
