/* sim/harness/nava_sim.h
 * CI-010: Core lifecycle — create mega1284 AVR at 16 MHz, load production ELF,
 * seed I2C EEPROM, run for N cycles, expose panel latch for button injection. */
#ifndef NAVA_SIM_H
#define NAVA_SIM_H

#include <stdint.h>
#include <stdbool.h>
#include "event_log.h"

struct avr_t;

/* Opaque sub-contexts — defined in their respective .c files */
struct nava_spi_ctx;
struct nava_gpio_ctx;
struct nava_lcd_ctx;
struct nava_midi_ctx;
struct nava_i2c_eeprom_ctx;

/* External I2C EEPROM is 24LC1024 = 128 KiB.
 * Firmware uses addresses 0-73791 (patterns + tracks + setup). */
#define NAVA_EEPROM_SIZE (128u * 1024u)

/* Timer math constants for bpm=120 (used by tests; do not use floats) */
#define NAVA_F_CPU              16000000UL
#define NAVA_TIMER1_PRESCALER   8UL
#define NAVA_PPQN               96UL
#define NAVA_DEFAULT_BPM        120UL
/* OCR1A = (F_CPU/prescaler) / ((bpm*PPQN)/60) — integer-truncating */
#define NAVA_OCR1A_120BPM  \
    ((NAVA_F_CPU / NAVA_TIMER1_PRESCALER) / \
     ((NAVA_DEFAULT_BPM * NAVA_PPQN) / 60UL))       /* = 10416 */
/* CTC period per PPQN tick (cycles) */
#define NAVA_PPQN_PERIOD_CYCLES ((NAVA_OCR1A_120BPM + 1UL) * NAVA_TIMER1_PRESCALER)  /* 83336 */
/* Timer2 trigger-off: OCR2A=249, prescaler=128 */
#define NAVA_TRIG_OFF_CYCLES    ((249UL + 1UL) * 128UL)  /* 32000 */
/* Timer3 flam[k]: (flam[k]+1)*64 cycles.  flam[0]=4999 → 320000 cycles=20ms */
#define NAVA_FLAM_CYCLES(ocr)   (((ocr) + 1UL) * 64UL)
/* OCR3A values from define.h flam[] array */
#define NAVA_FLAM_OCR(k) ((uint32_t[]){4999,5999,6999,7999,8999,9999,10999,11999}[k])

typedef struct {
    struct avr_t               *avr;
    event_log_t                 log;
    struct nava_spi_ctx        *spi;
    struct nava_gpio_ctx       *gpio;
    struct nava_lcd_ctx        *lcd;
    struct nava_midi_ctx       *midi;
    struct nava_i2c_eeprom_ctx *eeprom;
    /* Virtual 74HC165 parallel-input latch (5 bytes = dinSr[0..4] sources) */
    uint8_t                     panel_latch[5];
} nava_sim_t;

/* Create a sim context: load ELF into mega1284 at 16 MHz, attach peripherals.
 * Returns NULL on failure. */
nava_sim_t *nava_sim_create(const char *elf_path);

/* Destroy the sim context and free all resources. */
void nava_sim_destroy(nava_sim_t *ctx);

/* Seed the I2C EEPROM backing store before the first run.
 * The image must cover at least the setup block (offset 73728).
 * Called after nava_sim_create, before nava_sim_run_cycles. */
void nava_sim_seed_eeprom(nava_sim_t *ctx,
                           const uint8_t *image, size_t length);

/* Step the AVR for exactly n cycles.
 * Returns the final avr->cycle value, or 0 if the CPU halts/crashes. */
uint64_t nava_sim_run_cycles(nava_sim_t *ctx, uint64_t n);

/* Step until pred(ctx,user) returns true or max_cycles elapses.
 * Returns the number of cycles actually consumed. */
uint64_t nava_sim_run_until(nava_sim_t *ctx, uint64_t max_cycles,
                             bool (*pred)(nava_sim_t *, void *), void *user);

/* Set/clear one bit in the virtual 74HC165 parallel-input latch.
 * byte_idx: 0..4 (maps to dinSr[0..4]); bit_idx: 0..7. */
void nava_sim_panel_set_bit(nava_sim_t *ctx,
                             int byte_idx, int bit_idx, int level);

#endif /* NAVA_SIM_H */
