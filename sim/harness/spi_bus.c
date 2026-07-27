/* sim/harness/spi_bus.c
 * CI-020 on_spi_out       — route SPI bytes by active CS
 * CI-021 on_sw_cs_edge    — latch 74HC165 parallel inputs on SW_CS HIGH
 * CI-022 on_trig_cs_edge  — emit TRIG_WORD on TRIG_CS deassert
 * CI-023 on_dac_cs_edge   — emit DAC_WRITE + trigger MUX_ATTR on DAC_CS deassert
 * CI-024 on_led_cs_edge   — emit LED_WRITE on LED_CS deassert
 *
 * CS semantics (from define.h and Dio.ino):
 *   SW_CS   PD4: HIGH = latch+shift (SH/~LD semantics), LOW = idle
 *   LED_CS  PB4: LOW  = transfer active, HIGH = deasserted (5 bytes)
 *   TRIG_CS PB3: LOW  = transfer active, HIGH = deasserted (2 bytes)
 *   DAC_CS  PD5: LOW  = transfer active, HIGH = deasserted (2 bytes)
 */
#include "spi_bus.h"
#include "event_log.h"
#include "sim_avr.h"
#include "avr_spi.h"
#include "avr_ioport.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* ATmega I/O register offsets in data space (I/O addr + 0x20) */
#define AVR_PORTA_DATA  0x22u   /* PORTA I/O=0x02 */
#define AVR_PORTD_DATA  0x2Bu   /* PORTD I/O=0x0B */

typedef enum {
    CS_NONE = 0,
    CS_SW,    /* PD4 HIGH — 74HC165 shift mode  */
    CS_LED,   /* PB4 LOW  — LED chain            */
    CS_TRIG,  /* PB3 LOW  — trigger word         */
    CS_DAC,   /* PD5 LOW  — MCP4822 DAC          */
} active_cs_t;

struct nava_spi_ctx {
    struct avr_t *avr;
    event_log_t  *log;
    uint8_t      *panel_latch;   /* 5-byte virtual 74HC165 parallel inputs */

    active_cs_t   active_cs;

    /* 74HC165 shift-out state */
    uint8_t  hc165_buf[5];
    int      hc165_idx;
    /* Number of completed panel latches — the firmware only scans once it
     * reaches its main loop, so this is the definitive "ready for input"
     * signal (the boot splash writes the LCD long before polling starts). */
    uint64_t scan_count;

    /* Output accumulators */
    uint8_t  led_buf[5];   int led_count;
    uint8_t  trig_buf[2];  int trig_count;
    uint8_t  dac_buf[2];   int dac_count;

    /* PORTA/PORTD snapshot latched at DAC_CS falling edge */
    uint8_t  dac_porta_snap;
    uint8_t  dac_portd_snap;

    /* Optional callback for MUX_ATTR emission (set by gpio.c) */
    dac_write_cb_t dac_cb;
    void          *dac_cb_user;

    /* IRQ handle for feeding MISO bytes back to the firmware */
    avr_irq_t *spi_in_irq;
};

/* Optional front-panel scan trace, companion to NAVA_TWI_DEBUG.
 * Enable with: make -C sim CFLAGS_EXTRA=-DNAVA_SPI_DEBUG
 * Shows what the 74HC165 model hands back to the firmware, so a button that
 * "does nothing" can be told apart from a button the firmware never saw. */
#ifdef NAVA_SPI_DEBUG
#define SPI_DBG(fmt, ...) fprintf(stderr, "[SPI] " fmt "\n", ##__VA_ARGS__)
#else
#define SPI_DBG(fmt, ...) ((void)0)
#endif

/* ---- CI-020: on_spi_out — route each SPI byte by active CS ---- */
static void on_spi_out(struct avr_irq_t *irq, uint32_t value, void *param) {
    struct nava_spi_ctx *ctx = (struct nava_spi_ctx *)param;
    uint8_t byte = (uint8_t)(value & 0xFF);

    switch (ctx->active_cs) {
    case CS_SW:
        /* Shift out next 74HC165 latch byte onto MISO for the firmware.
         * The firmware writes 0 as a dummy; we return the latch byte. */
        if (ctx->hc165_idx < 5) {
            avr_raise_irq(ctx->spi_in_irq, ctx->hc165_buf[ctx->hc165_idx]);
            ctx->hc165_idx++;
        } else {
            avr_raise_irq(ctx->spi_in_irq, 0xFF);
        }
        (void)byte;
        break;

    case CS_LED:
        if (ctx->led_count < 5) ctx->led_buf[ctx->led_count++] = byte;
        break;

    case CS_TRIG:
        if (ctx->trig_count < 2) ctx->trig_buf[ctx->trig_count++] = byte;
        break;

    case CS_DAC:
        if (ctx->dac_count < 2) ctx->dac_buf[ctx->dac_count++] = byte;
        break;

    default:
        break;
    }
}

/* ---- CI-021: on_sw_cs_edge — latch 74HC165 parallel inputs on SW_CS HIGH ---- */
static void on_sw_cs_edge(struct avr_irq_t *irq, uint32_t value, void *param) {
    struct nava_spi_ctx *ctx = (struct nava_spi_ctx *)param;
    int level = (int)(value & 1u);
    SPI_DBG("sw_cs edge level=%d latch [%02X %02X %02X %02X %02X]", level,
            ctx->panel_latch[0], ctx->panel_latch[1], ctx->panel_latch[2],
            ctx->panel_latch[3], ctx->panel_latch[4]);

    if (level == 1) {
        /* Rising edge = SH/~LD HIGH = shift mode begins.
         * Latch the current virtual panel inputs NOW so subsequent SPI
         * transfers stream dinSr[0..4] from this snapshot. */
        memcpy(ctx->hc165_buf, ctx->panel_latch, 5);
        ctx->hc165_idx = 0;
        ctx->active_cs = CS_SW;
        ctx->scan_count++;
        if (ctx->panel_latch[0] | ctx->panel_latch[1] | ctx->panel_latch[2] |
            ctx->panel_latch[3] | ctx->panel_latch[4])
            SPI_DBG("latch %02X %02X %02X %02X %02X",
                    ctx->panel_latch[0], ctx->panel_latch[1],
                    ctx->panel_latch[2], ctx->panel_latch[3],
                    ctx->panel_latch[4]);
    } else {
        /* Falling edge = scan complete */
        if (ctx->active_cs == CS_SW) ctx->active_cs = CS_NONE;
    }
}

/* ---- CI-024: on_led_cs_edge ---- */
static void on_led_cs_edge(struct avr_irq_t *irq, uint32_t value, void *param) {
    struct nava_spi_ctx *ctx = (struct nava_spi_ctx *)param;
    int level = (int)(value & 1u);

    if (level == 0) {
        /* Falling edge = CS asserted, begin accumulation */
        ctx->led_count = 0;
        ctx->active_cs = CS_LED;
    } else {
        /* Rising edge = deassert; emit LED_WRITE event */
        if (ctx->active_cs == CS_LED && ctx->led_count > 0) {
            sim_event_t evt = { .cycle = ctx->avr->cycle, .type = EVT_LED_WRITE };
            memcpy(evt.led.bytes, ctx->led_buf,
                   (size_t)ctx->led_count < 5u ? (size_t)ctx->led_count : 5u);
            event_log_append(ctx->log, &evt);
        }
        ctx->active_cs = CS_NONE;
    }
}

/* ---- CI-022: on_trig_cs_edge ---- */
static void on_trig_cs_edge(struct avr_irq_t *irq, uint32_t value, void *param) {
    struct nava_spi_ctx *ctx = (struct nava_spi_ctx *)param;
    int level = (int)(value & 1u);

    if (level == 0) {
        ctx->trig_count = 0;
        ctx->active_cs  = CS_TRIG;
    } else {
        /* Deasserted after 2 bytes — emit TRIG_WORD.
         * Do NOT assert a zero word: tempDoutTrig may retain bit B10 (HH-select)
         * from the CH/OH circuit suppression logic, so the Timer2 restore write
         * can be non-zero (DL-016). */
        if (ctx->active_cs == CS_TRIG && ctx->trig_count == 2) {
            sim_event_t evt = {
                .cycle     = ctx->avr->cycle,
                .type      = EVT_TRIG_WORD,
                .trig_word = (uint16_t)((ctx->trig_buf[0] << 8) |
                                          ctx->trig_buf[1]),
            };
            event_log_append(ctx->log, &evt);
        }
        ctx->active_cs = CS_NONE;
    }
}

/* ---- CI-023: on_dac_cs_edge ---- */
static void on_dac_cs_edge(struct avr_irq_t *irq, uint32_t value, void *param) {
    struct nava_spi_ctx *ctx = (struct nava_spi_ctx *)param;
    int level = (int)(value & 1u);

    if (level == 0) {
        /* CS asserted: sample PORTA/PORTD for voice attribution.
         * Note: SetMux sets PORTA AFTER calling SetDacA, so the address here
         * corresponds to the previous voice's routing — the timing limitation
         * documented in DL-009.  Tests rely on MIDI notes, not MUX_ATTR, so
         * this off-by-one in the event log does not break any assertions. */
        ctx->dac_porta_snap = ctx->avr->data[AVR_PORTA_DATA];
        ctx->dac_portd_snap = ctx->avr->data[AVR_PORTD_DATA];
        ctx->dac_count      = 0;
        ctx->active_cs      = CS_DAC;
    } else {
        /* Deasserted after 2 bytes: decode MCP4822 command word and emit events */
        if (ctx->active_cs == CS_DAC && ctx->dac_count == 2) {
            uint16_t word     = (uint16_t)((ctx->dac_buf[0] << 8) | ctx->dac_buf[1]);
            /* Channel A = bit 12 (0x1000 set); velocity = (word & 0x0FFF) >> 5
             * (matches SetDacA: milliVolt = velocity << 5) */
            uint8_t  channel  = (word & 0x1000u) ? 0u : 1u;
            uint8_t  velocity = (uint8_t)((word & 0x0FFFu) >> 5u);

            sim_event_t dac_evt = {
                .cycle        = ctx->avr->cycle,
                .type         = EVT_DAC_WRITE,
                .dac.channel  = channel,
                .dac.velocity = velocity,
            };
            event_log_append(ctx->log, &dac_evt);

            /* Notify gpio.c to emit the paired MUX_ATTR event */
            if (ctx->dac_cb) ctx->dac_cb(velocity, ctx->dac_cb_user);
        }
        ctx->active_cs = CS_NONE;
    }
}

/* ---- Public API ---- */

nava_spi_ctx_t *nava_spi_attach(struct avr_t *avr, event_log_t *log,
                                  uint8_t *panel_latch) {
    struct nava_spi_ctx *ctx = calloc(1, sizeof(*ctx));
    assert(ctx);
    ctx->avr         = avr;
    ctx->log         = log;
    ctx->panel_latch = panel_latch;
    ctx->active_cs   = CS_NONE;

    /* SPI0 output IRQ (firmware TX → our model) */
    avr_irq_t *spi_out = avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0),
                                         SPI_IRQ_OUTPUT);
    if (spi_out) avr_irq_register_notify(spi_out, on_spi_out, ctx);

    /* SPI0 input IRQ (our model → firmware RX/MISO) */
    ctx->spi_in_irq = avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0),
                                      SPI_IRQ_INPUT);

    /* CS GPIO edges — use IOPORT_IRQ_PINn for individual pin notifications */
    /* PD4 = SW_CS */
    avr_irq_t *pd4 = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'),
                                     IOPORT_IRQ_PIN4);
    if (pd4) avr_irq_register_notify(pd4, on_sw_cs_edge, ctx);

    /* PB4 = LED_CS */
    avr_irq_t *pb4 = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'),
                                     IOPORT_IRQ_PIN4);
    if (pb4) avr_irq_register_notify(pb4, on_led_cs_edge, ctx);

    /* PB3 = TRIG_CS */
    avr_irq_t *pb3 = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'),
                                     IOPORT_IRQ_PIN3);
    if (pb3) avr_irq_register_notify(pb3, on_trig_cs_edge, ctx);

    /* PD5 = DAC_CS */
    avr_irq_t *pd5 = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'),
                                     IOPORT_IRQ_PIN5);
    if (pd5) avr_irq_register_notify(pd5, on_dac_cs_edge, ctx);

    return ctx;
}

void nava_spi_detach(nava_spi_ctx_t *ctx) { free(ctx); }

void nava_spi_get_dac_snap(const nava_spi_ctx_t *ctx,
                             uint8_t *porta_out, uint8_t *portd_out) {
    *porta_out = ctx->dac_porta_snap;
    *portd_out = ctx->dac_portd_snap;
}

void nava_spi_set_dac_write_cb(nava_spi_ctx_t *ctx,
                                 dac_write_cb_t cb, void *user) {
    ctx->dac_cb      = cb;
    ctx->dac_cb_user = user;
}

uint64_t nava_spi_scan_count(const nava_spi_ctx_t *ctx) {
    return ctx ? ctx->scan_count : 0;
}
