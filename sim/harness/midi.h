/* sim/harness/midi.h
 * CI-050..CI-052: USART1 MIDI TX capture (running-status + real-time
 * transparent) and RX injection for slave-sync tests. */
#ifndef NAVA_MIDI_H
#define NAVA_MIDI_H

#include <stdint.h>
#include <stddef.h>
#include "event_log.h"

struct avr_t;
typedef struct nava_midi_ctx nava_midi_ctx_t;

nava_midi_ctx_t *nava_midi_attach(struct avr_t *avr, event_log_t *log);
void nava_midi_detach(nava_midi_ctx_t *ctx);

/* Inject raw bytes into USART1 RX.
 * Bytes are delivered at 31250-baud timing (~320 µs/byte = 5120 cycles).
 * Advances the AVR clock internally to space the bytes correctly. */
void nava_midi_inject_bytes(nava_midi_ctx_t *ctx,
                             const uint8_t *bytes, size_t len);

/* Convenience injectors */
void nava_midi_inject_clock(nava_midi_ctx_t *ctx);   /* 0xF8 */
void nava_midi_inject_start(nava_midi_ctx_t *ctx);   /* 0xFA */
void nava_midi_inject_stop(nava_midi_ctx_t *ctx);    /* 0xFC */

/* Find the first decoded note-on in the event log matching channel and note
 * (note is the wire value, i.e. MIDI note + 12).  Returns NULL if not found.
 * cycle_lo/hi: search window (hi==0 → unbounded). */
const sim_event_t *nava_midi_expect_note_on(const event_log_t *log,
                                              uint8_t channel, uint8_t wire_note,
                                              uint64_t cycle_lo, uint64_t cycle_hi);
const sim_event_t *nava_midi_expect_note_off(const event_log_t *log,
                                               uint8_t channel, uint8_t wire_note,
                                               uint64_t cycle_lo, uint64_t cycle_hi);

/* Count real-time clock bytes (0xF8) in a cycle window */
size_t nava_midi_count_clocks(const event_log_t *log,
                               uint64_t cycle_lo, uint64_t cycle_hi);

#endif /* NAVA_MIDI_H */
