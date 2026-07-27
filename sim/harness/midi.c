/* sim/harness/midi.c
 * CI-050 on_uart_tx       — USART1 TX capture with running-status + RT-bypass
 * CI-051 midi_inject_bytes — deliver bytes to USART1 RX at 31250-baud timing
 * CI-052 midi_expect_note  — decoded note query helpers
 *
 * Key firmware MIDI facts (verified in Midi.ino and Clock.ino):
 *   - MIDI.sendNoteOn/Off adds +12 to the note (Midi.ino:36,43), so EXT_INST
 *     notes 36-51 appear on wire as 48-63 (0x30-0x3F).
 *   - Clock.ino writes 0xF8 directly to UDR1, bypassing the MIDI library; it
 *     can appear mid-message.
 *   - MIDI library may use running status (status byte omitted when repeated).
 *   - MIDI_DRUMNOTES_OUT is commented out in features.h, so the ONLY note
 *     traffic is EXT_INST. Drum notes are compiled out entirely.
 */
#include "midi.h"
#include "sim_avr.h"
#include "avr_uart.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* 31250 baud at 16 MHz: 1 byte = 10 bits = 5120 CPU cycles */
#define MIDI_BYTE_CYCLES 5120u

/* MIDI parser state for running-status reconstruction */
typedef enum {
    PARSE_IDLE = 0,
    PARSE_NOTE_ON_1,    /* waiting for note byte */
    PARSE_NOTE_ON_2,    /* waiting for velocity byte */
    PARSE_NOTE_OFF_1,
    PARSE_NOTE_OFF_2,
} parse_state_t;

struct nava_midi_ctx {
    struct avr_t   *avr;
    event_log_t    *log;

    /* Running-status parser state */
    parse_state_t   state;
    uint8_t         running_status;  /* last channel-message status byte */
    uint8_t         note_buf;        /* first data byte buffer */

    /* USART1 IRQ handle for injecting RX bytes */
    avr_irq_t      *uart_rx_irq;
};

/* CI-050: called on every USART1 TX byte */
static void on_uart_tx(struct avr_irq_t *irq, uint32_t value, void *param) {
    struct nava_midi_ctx *ctx = (struct nava_midi_ctx *)param;
    uint8_t byte = (uint8_t)(value & 0xFF);

    /* Log every raw byte unconditionally */
    sim_event_t raw_evt = {
        .cycle    = ctx->avr->cycle,
        .type     = EVT_MIDI_TX_BYTE,
        .raw_byte = byte,
    };
    event_log_append(ctx->log, &raw_evt);

    /* Real-time bytes (0xF8 clock, 0xFA start, 0xFB continue, 0xFC stop)
     * are transparently routed to a separate EVT_MIDI_RT record and do NOT
     * disturb the running-status channel-message state machine. */
    if (byte >= 0xF8u) {
        sim_event_t rt_evt = {
            .cycle    = ctx->avr->cycle,
            .type     = EVT_MIDI_RT,
            .raw_byte = byte,
        };
        event_log_append(ctx->log, &rt_evt);
        return;
    }

    /* Status byte — updates running status and resets parser */
    if (byte & 0x80u) {
        ctx->running_status = byte;
        uint8_t msg_type = byte & 0xF0u;
        if (msg_type == 0x90u)      ctx->state = PARSE_NOTE_ON_1;
        else if (msg_type == 0x80u) ctx->state = PARSE_NOTE_OFF_1;
        else                         ctx->state = PARSE_IDLE;
        return;
    }

    /* Data byte — reconstruct message using running status */
    switch (ctx->state) {
    case PARSE_NOTE_ON_1:
        ctx->note_buf = byte;
        ctx->state    = PARSE_NOTE_ON_2;
        break;
    case PARSE_NOTE_ON_2: {
        uint8_t channel  = (ctx->running_status & 0x0Fu) + 1u; /* 1-based */
        uint8_t note     = ctx->note_buf;
        uint8_t velocity = byte;
        /* Note-on with velocity 0 is treated as note-off by convention */
        if (velocity == 0) {
            sim_event_t evt = {
                .cycle            = ctx->avr->cycle,
                .type             = EVT_MIDI_NOTE_OFF,
                .midi_note.channel = channel,
                .midi_note.note   = note,
                .midi_note.velocity = 0,
            };
            event_log_append(ctx->log, &evt);
        } else {
            sim_event_t evt = {
                .cycle             = ctx->avr->cycle,
                .type              = EVT_MIDI_NOTE_ON,
                .midi_note.channel = channel,
                .midi_note.note    = note,
                .midi_note.velocity = velocity,
            };
            event_log_append(ctx->log, &evt);
        }
        /* Running status: next pair of data bytes restarts from NOTE_ON_1 */
        ctx->state = PARSE_NOTE_ON_1;
        break;
    }
    case PARSE_NOTE_OFF_1:
        ctx->note_buf = byte;
        ctx->state    = PARSE_NOTE_OFF_2;
        break;
    case PARSE_NOTE_OFF_2: {
        uint8_t channel = (ctx->running_status & 0x0Fu) + 1u;
        sim_event_t evt = {
            .cycle             = ctx->avr->cycle,
            .type              = EVT_MIDI_NOTE_OFF,
            .midi_note.channel = channel,
            .midi_note.note    = ctx->note_buf,
            .midi_note.velocity = byte,
        };
        event_log_append(ctx->log, &evt);
        ctx->state = PARSE_NOTE_OFF_1;
        break;
    }
    default:
        break;
    }
}

nava_midi_ctx_t *nava_midi_attach(struct avr_t *avr, event_log_t *log) {
    struct nava_midi_ctx *ctx = calloc(1, sizeof(*ctx));
    assert(ctx);
    ctx->avr   = avr;
    ctx->log   = log;
    ctx->state = PARSE_IDLE;

    /* Hook USART1 TX (firmware writes → our capture) */
    avr_irq_t *tx_irq = avr_io_getirq(avr,
                                        AVR_IOCTL_UART_GETIRQ('1'),
                                        UART_IRQ_OUTPUT);
    if (tx_irq) avr_irq_register_notify(tx_irq, on_uart_tx, ctx);

    /* Retain USART1 RX IRQ for injection */
    ctx->uart_rx_irq = avr_io_getirq(avr,
                                       AVR_IOCTL_UART_GETIRQ('1'),
                                       UART_IRQ_INPUT);

    /* Turn OFF simavr's AVR_UART_FLAG_STDIO on both UARTs.  With it set,
     * avr_uart.c echoes every transmitted byte to STDOUT (in green, control
     * bytes rendered as '.').  On this firmware that dumps the raw MIDI stream
     * into the tests' TAP output and corrupts it — MIDI is binary, so the echo
     * also emits invalid UTF-8 that breaks downstream text tools. */
    for (const char *u = "01"; *u; u++) {
        uint32_t flags = 0;
        /* avr_ioctl returns 0 on success (NOT the ctl value). */
        if (avr_ioctl(avr, AVR_IOCTL_UART_GET_FLAGS(*u), &flags) == 0) {
            flags &= ~(uint32_t)AVR_UART_FLAG_STDIO;
            avr_ioctl(avr, AVR_IOCTL_UART_SET_FLAGS(*u), &flags);
        }
    }
    return ctx;
}

void nava_midi_detach(nava_midi_ctx_t *ctx) { free(ctx); }

/* CI-051: inject bytes at correct baud timing so the firmware's ISR picks them up */
void nava_midi_inject_bytes(nava_midi_ctx_t *ctx,
                             const uint8_t *bytes, size_t len) {
    if (!ctx->uart_rx_irq) return;
    struct avr_t *avr = ctx->avr;
    for (size_t i = 0; i < len; i++) {
        avr_raise_irq(ctx->uart_rx_irq, bytes[i]);
        /* Advance one byte-time so the UART ISR can fire before the next byte */
        uint64_t target = avr->cycle + MIDI_BYTE_CYCLES;
        while (avr->cycle < target) avr_run(avr);
    }
}

void nava_midi_inject_clock(nava_midi_ctx_t *ctx) {
    uint8_t b = 0xF8u;
    nava_midi_inject_bytes(ctx, &b, 1);
}
void nava_midi_inject_start(nava_midi_ctx_t *ctx) {
    uint8_t b = 0xFAu;
    nava_midi_inject_bytes(ctx, &b, 1);
}
void nava_midi_inject_stop(nava_midi_ctx_t *ctx) {
    uint8_t b = 0xFCu;
    nava_midi_inject_bytes(ctx, &b, 1);
}

/* CI-052: note query helpers */
static const sim_event_t *find_note(const event_log_t *log,
                                     event_type_t type,
                                     uint8_t channel, uint8_t wire_note,
                                     uint64_t lo, uint64_t hi) {
    for (size_t i = 0; i < log->count; i++) {
        const sim_event_t *e = &log->buf[i];
        if (e->type != type) continue;
        if (hi && e->cycle > hi) continue;
        if (e->cycle < lo) continue;
        if (channel && e->midi_note.channel != channel) continue;
        if (wire_note && e->midi_note.note != wire_note) continue;
        return e;
    }
    return NULL;
}

const sim_event_t *nava_midi_expect_note_on(const event_log_t *log,
                                              uint8_t channel, uint8_t wire_note,
                                              uint64_t lo, uint64_t hi) {
    return find_note(log, EVT_MIDI_NOTE_ON, channel, wire_note, lo, hi);
}
const sim_event_t *nava_midi_expect_note_off(const event_log_t *log,
                                               uint8_t channel, uint8_t wire_note,
                                               uint64_t lo, uint64_t hi) {
    return find_note(log, EVT_MIDI_NOTE_OFF, channel, wire_note, lo, hi);
}

size_t nava_midi_count_clocks(const event_log_t *log,
                               uint64_t lo, uint64_t hi) {
    size_t n = 0;
    for (size_t i = 0; i < log->count; i++) {
        const sim_event_t *e = &log->buf[i];
        if (e->type == EVT_MIDI_RT && e->raw_byte == 0xF8u &&
            e->cycle >= lo && (hi == 0 || e->cycle <= hi)) n++;
    }
    return n;
}
