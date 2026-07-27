/* sim/harness/gpio.c
 * CI-030 sample_mux_attribution — voice+velocity at DAC_CS deassert
 * CI-031 on_porta_change       — log DIN clock and TRIG_OUT edges
 * CI-032 inject_encoder        — quadrature injection on PINB0/1
 * CI-033 set_encoder_switch    — drive PINB2
 *
 * muxAddr[] from define.h (PORTA[7:5] used as address, upper 3 bits):
 *   idx 0 → 0x00 (B00000000 masked to bits 7:5)
 *   idx 1 → 0x80 (B10000000)
 *   idx 2 → 0x40 (B01000000)
 *   idx 3 → 0xC0 (B11000000)
 *   idx 4 → 0x20 (B00100000)
 * muxInst[] = {LT, SD, BD, MT, HT, HC, RM, CH, CRASH, RIDE}
 * INH1=PD6 → first mux (indices 0..4), INH2=PD7 → second mux (indices 5..9)
 */
#include "gpio.h"
#include "nava_sim.h"
#include "sim_avr.h"
#include "avr_ioport.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

static const uint8_t MUX_ADDR_TABLE[5] = { 0x00, 0x80, 0x40, 0xC0, 0x20 };
static const char   *VOICE_NAMES[VOICE_COUNT] = {
    "LT","SD","BD","MT","HT","HC","RM","CH","CRASH","RIDE"
};

const char *voice_name(voice_t v) {
    if (v >= 0 && v < VOICE_COUNT) return VOICE_NAMES[v];
    return "?";
}

struct nava_gpio_ctx {
    struct avr_t   *avr;
    event_log_t    *log;
    nava_spi_ctx_t *spi;
    uint8_t         prev_porta;
};

/* CI-030: emitted from the spi_bus DAC_WRITE callback.
 * Reads PORTA/PORTD snapshots that spi_bus captured at DAC_CS-falling edge. */
static void sample_mux_attribution(uint8_t velocity, void *user) {
    struct nava_gpio_ctx *ctx = (struct nava_gpio_ctx *)user;
    uint8_t porta, portd;
    nava_spi_get_dac_snap(ctx->spi, &porta, &portd);

    /* Decode PORTA[7:5] to mux slot index */
    uint8_t addr_bits = porta & 0xE0u;
    int slot = -1;
    for (int i = 0; i < 5; i++) {
        if (addr_bits == MUX_ADDR_TABLE[i]) { slot = i; break; }
    }
    if (slot < 0) return;

    /* INH1 LOW (PD6=0) → first mux, INH2 LOW (PD7=0) → second mux */
    int inh1_low = !(portd & (1u << 6));
    int inh2_low = !(portd & (1u << 7));

    voice_t voice;
    if (inh1_low)      voice = (voice_t)slot;
    else if (inh2_low) voice = (voice_t)(slot + 5);
    else               return;  /* both inhibits high = no voice active */

    sim_event_t evt = {
        .cycle        = ctx->avr->cycle,
        .type         = EVT_MUX_ATTR,
        .mux.voice    = (uint8_t)voice,
        .mux.velocity = velocity,
    };
    event_log_append(ctx->log, &evt);
}

/* CI-031: log DIN clock (PORTA1) and TRIG_OUT (PORTA2) transitions */
static void on_porta_change(struct avr_irq_t *irq, uint32_t value, void *param) {
    struct nava_gpio_ctx *ctx = (struct nava_gpio_ctx *)param;
    uint8_t porta   = (uint8_t)(value & 0xFF);
    uint8_t changed = porta ^ ctx->prev_porta;
    ctx->prev_porta = porta;

    if (changed & (1u << 1)) {  /* DIN clock = PORTA1 */
        sim_event_t evt = {
            .cycle = ctx->avr->cycle,
            .type  = EVT_DIN_CLK,
            .level = (uint8_t)((porta >> 1) & 1u),
        };
        event_log_append(ctx->log, &evt);
    }
    if (changed & (1u << 2)) {  /* TRIG_OUT = PORTA2 */
        sim_event_t evt = {
            .cycle = ctx->avr->cycle,
            .type  = EVT_TRIG_OUT,
            .level = (uint8_t)((porta >> 2) & 1u),
        };
        event_log_append(ctx->log, &evt);
    }
}

nava_gpio_ctx_t *nava_gpio_attach(struct avr_t *avr, event_log_t *log,
                                   nava_spi_ctx_t *spi) {
    struct nava_gpio_ctx *ctx = calloc(1, sizeof(*ctx));
    assert(ctx);
    ctx->avr = avr;
    ctx->log = log;
    ctx->spi = spi;

    /* Watch PORTA for DIN clock and TRIG_OUT.  IOPORT_IRQ_PIN_ALL is the
     * whole-port IRQ: it delivers the full 8-bit port value, which
     * on_porta_change diffs against prev_porta to find edges. */
    avr_irq_t *porta_irq = avr_io_getirq(avr,
                                           AVR_IOCTL_IOPORT_GETIRQ('A'),
                                           IOPORT_IRQ_PIN_ALL);
    if (porta_irq) avr_irq_register_notify(porta_irq, on_porta_change, ctx);

    /* Register the DAC_WRITE callback so we emit MUX_ATTR events */
    nava_spi_set_dac_write_cb(spi, sample_mux_attribution, ctx);

    return ctx;
}

void nava_gpio_detach(nava_gpio_ctx_t *ctx) { free(ctx); }

/* CI-032: inject quadrature encoder pulses on PINB0 (A) / PINB1 (B).
 *
 * Enc.ino POLLS these pins from the main loop (~220 Hz, i.e. one sample every
 * ~72700 cycles) rather than using pin-change interrupts.  Two consequences
 * drive the implementation below:
 *
 *  1. Each quadrature phase must be held for at least one poll interval, or
 *     whole detents slip between samples and are never seen.  (Holding a phase
 *     for ~3200 cycles, as this originally did, lost most of them.)
 *  2. The full 4-phase Gray sequence must be emitted.  Skipping a phase — or
 *     dropping A and B together — produces a transition the decoder cannot
 *     attribute to a direction, so counts come out short and the sign is
 *     unreliable.
 *
 * CW:  00 -> 10 -> 11 -> 01 -> 00      CCW: 00 -> 01 -> 11 -> 10 -> 00 */
#define ENC_PHASE_CYCLES 90000u   /* > one ~72700-cycle poll interval */

void nava_gpio_inject_encoder(nava_gpio_ctx_t *ctx,
                               int direction, int detents) {
    avr_irq_t *pb0 = avr_io_getirq(ctx->avr,
                                     AVR_IOCTL_IOPORT_GETIRQ('B'),
                                     IOPORT_IRQ_PIN0);
    avr_irq_t *pb1 = avr_io_getirq(ctx->avr,
                                     AVR_IOCTL_IOPORT_GETIRQ('B'),
                                     IOPORT_IRQ_PIN1);
    if (!pb0 || !pb1) return;

    /* (A,B) phases in order; reversed for CCW. */
    static const uint8_t PHASE[4][2] = { {1,0}, {1,1}, {0,1}, {0,0} };

    struct avr_t *avr = ctx->avr;
    for (int d = 0; d < detents; d++) {
        for (int p = 0; p < 4; p++) {
            int idx = (direction >= 0) ? p : (3 - p);
            avr_raise_irq(pb0, PHASE[idx][0]);
            avr_raise_irq(pb1, PHASE[idx][1]);
            uint64_t target = avr->cycle + ENC_PHASE_CYCLES;
            while (avr->cycle < target) avr_run(avr);
        }
    }
}

/* CI-033: drive PINB2 (BTN_ENCODER / ENC_SW_GET) */
void nava_gpio_set_encoder_switch(nava_gpio_ctx_t *ctx, int pressed) {
    avr_irq_t *pb2 = avr_io_getirq(ctx->avr,
                                     AVR_IOCTL_IOPORT_GETIRQ('B'),
                                     IOPORT_IRQ_PIN2);
    if (pb2) avr_raise_irq(pb2, pressed ? 1u : 0u);
}
