/* sim/harness/spi_bus.h
 * CI-020..CI-024: SPI peripheral models — 74HC165 input chain, 74HC595 LED
 * and trigger output chains, MCP4822 DAC.  CS demux routes bytes by the GPIO
 * line asserted at transfer time (not by SPI address). */
#ifndef NAVA_SPI_BUS_H
#define NAVA_SPI_BUS_H

#include <stdint.h>
#include "event_log.h"

struct avr_t;
typedef struct nava_spi_ctx nava_spi_ctx_t;

/* Attach all SPI peripheral models to the AVR.
 * panel_latch: pointer to nava_sim_t.panel_latch[5] — the shared virtual
 * 74HC165 parallel inputs that press_button() mutates. */
nava_spi_ctx_t *nava_spi_attach(struct avr_t *avr, event_log_t *log,
                                  uint8_t *panel_latch);
void nava_spi_detach(nava_spi_ctx_t *ctx);

/* Called by gpio.c's sample_mux_attribution at DAC_CS deassert time.
 * Returns the PORTA and PORTD snapshots latched at the most recent DAC_CS
 * falling edge (= when firmware started the SPI transfer). */
void nava_spi_get_dac_snap(const nava_spi_ctx_t *ctx,
                             uint8_t *porta_out, uint8_t *portd_out);

/* Called by gpio.c to register a callback for DAC_WRITE events so it can
 * emit the paired MUX_ATTR event at the same cycle. */
typedef void (*dac_write_cb_t)(uint8_t velocity, void *user);
void nava_spi_set_dac_write_cb(nava_spi_ctx_t *ctx,
                                 dac_write_cb_t cb, void *user);

/* Number of front-panel latches (74HC165 loads) the firmware has performed.
 * Zero while the firmware is still in its boot splash: panel scanning only
 * starts once the main loop runs, so tests must wait for this to advance
 * before injecting button presses, or the presses are simply never sampled. */
uint64_t nava_spi_scan_count(const nava_spi_ctx_t *ctx);

#endif /* NAVA_SPI_BUS_H */
