/* sim/harness/lcd.c
 * CI-040 lcd_attach   — wire HD44780 part to PC2/PC3/PC4-7, 4-bit write-only
 * CI-041 lcd_get_line — return rendered 16-char row from DDRAM mirror
 *
 * The firmware uses LiquidCrystal with RS=PC2, EN=PC3, D4=PC4..D7=PC7.
 * There is no RW pin so the busy flag is never read — the part must not stall
 * waiting for one.
 *
 * simavr's HD44780 part (simavr/examples/parts/hd44780.h) provides:
 *   hd44780_t, hd44780_init(), hd44780_connect(), hd44780_get_line()
 * and fires an IRQ each time a character is written to DDRAM.
 * We mirror the rendered display into a 2×17 char buffer for test assertions.
 */
#include "lcd.h"
#include "sim_avr.h"
#include "avr_ioport.h"
#include "hd44780.h"      /* from sim/simavr/examples/parts/hd44780.h */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* 16×2 display, each line stored as 16 chars + NUL */
#define LCD_COLS 16
#define LCD_ROWS 2

struct nava_lcd_ctx {
    struct avr_t  *avr;
    event_log_t   *log;
    hd44780_t      part;
    char           screen[LCD_ROWS][LCD_COLS + 1];
    int            active;     /* non-zero once first DDRAM write received */
};

/* Sink for the vendored hd44780 part's debug output.  That part printf()s every
 * data/command byte to stdout, which would interleave with and corrupt the
 * tests' TAP stream.  setup_simavr.sh compiles hd44780.c with
 * -Dprintf=nava_hd44780_quiet so its chatter lands here instead; the pinned
 * submodule source is never edited. */
int nava_hd44780_quiet(const char *fmt, ...);
int nava_hd44780_quiet(const char *fmt, ...) { (void)fmt; return 0; }

/* Mirror the part's DDRAM into the text buffer.
 * The part exposes no line accessor, so read vram directly.  On a 2-line
 * HD44780 the rows are NOT contiguous: row 0 starts at DDRAM 0x00 and row 1 at
 * 0x40, regardless of column count. */
static void refresh_screen(struct nava_lcd_ctx *ctx) {
    static const uint8_t row_base[LCD_ROWS] = { 0x00, 0x40 };
    for (int row = 0; row < LCD_ROWS; row++) {
        const uint8_t *src = &ctx->part.vram[row_base[row]];
        for (int col = 0; col < LCD_COLS; col++) {
            uint8_t c = src[col];
            /* Firmware writes custom CGRAM glyphs (codes 0-7) for the step
             * markers; render them as '.' so text assertions stay readable. */
            ctx->screen[row][col] = (c >= 0x20 && c < 0x80) ? (char)c : '.';
        }
        ctx->screen[row][LCD_COLS] = '\0';
    }
}

/* Callback from hd44780 part when DDRAM content changes */
static void on_lcd_char(struct avr_irq_t *irq, uint32_t value, void *param) {
    struct nava_lcd_ctx *ctx = (struct nava_lcd_ctx *)param;
    refresh_screen(ctx);
    ctx->active = 1;

    /* Log the event for timing correlation */
    sim_event_t evt = {
        .cycle = ctx->avr->cycle,
        .type  = EVT_LCD_CHAR,
    };
    event_log_append(ctx->log, &evt);
    (void)value;
}

/* CI-040: instantiate the HD44780 part and connect firmware LCD pins */
nava_lcd_ctx_t *nava_lcd_attach(struct avr_t *avr, event_log_t *log) {
    struct nava_lcd_ctx *ctx = calloc(1, sizeof(*ctx));
    assert(ctx);
    ctx->avr = avr;
    ctx->log = log;
    /* Pre-fill display with spaces */
    for (int r = 0; r < LCD_ROWS; r++) {
        memset(ctx->screen[r], ' ', LCD_COLS);
        ctx->screen[r][LCD_COLS] = '\0';
    }

    /* Initialize the HD44780 part: 4-bit, 16 columns, 2 rows */
    hd44780_init(avr, &ctx->part, LCD_COLS, LCD_ROWS);

    /* Connect firmware pins to the HD44780 part IRQs.
     * Firmware: RS=PC2, EN=PC3, D4=PC4, D5=PC5, D6=PC6, D7=PC7 */
    struct {
        char port; int pin; int hd_irq;
    } wires[] = {
        { 'C', 2, IRQ_HD44780_RS  },
        { 'C', 3, IRQ_HD44780_E   },
        { 'C', 4, IRQ_HD44780_D4  },
        { 'C', 5, IRQ_HD44780_D5  },
        { 'C', 6, IRQ_HD44780_D6  },
        { 'C', 7, IRQ_HD44780_D7  },
    };
    for (size_t i = 0; i < sizeof(wires)/sizeof(wires[0]); i++) {
        avr_irq_t *pin_irq = avr_io_getirq(avr,
                                 AVR_IOCTL_IOPORT_GETIRQ(wires[i].port),
                                 IOPORT_IRQ_PIN0 + wires[i].pin);
        if (pin_irq) {
            avr_connect_irq(pin_irq, &ctx->part.irq[wires[i].hd_irq]);
        }
    }

    /* Register a callback on the HD44780's character-written notification */
    avr_irq_register_notify(&ctx->part.irq[IRQ_HD44780_BUSY],
                             on_lcd_char, ctx);

    return ctx;
}

void nava_lcd_detach(nava_lcd_ctx_t *ctx) { free(ctx); }

/* CI-041 */
const char *nava_lcd_get_line(const nava_lcd_ctx_t *ctx, int row) {
    if (row < 0 || row >= LCD_ROWS) return NULL;
    /* Re-read vram on every call rather than trusting that a notification IRQ
     * fired: the part raises IRQ_HD44780_BUSY on command completion, not on
     * every DDRAM write, so a callback-only mirror can lag the real display.
     * The cast is safe — this refreshes a cache, it does not mutate the LCD. */
    refresh_screen((struct nava_lcd_ctx *)ctx);
    return ctx->screen[row];
}

const uint8_t *nava_lcd_get_raw(const nava_lcd_ctx_t *ctx, int row) {
    static const uint8_t row_base[LCD_ROWS] = { 0x00, 0x40 };
    if (row < 0 || row >= LCD_ROWS) return NULL;
    return &ctx->part.vram[row_base[row]];
}

int nava_lcd_is_active(const nava_lcd_ctx_t *ctx) { return ctx->active; }
