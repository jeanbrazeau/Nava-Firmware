/* sim/harness/nava_sim.c
 * CI-010 nava_sim_create  — load ELF, init mega1284 at 16 MHz, attach peripherals
 * CI-011 nava_sim_seed_eeprom — fill I2C EEPROM model before first firmware read
 * CI-012 nava_sim_run_cycles  — deterministic cycle stepping with halt detection */
#include "nava_sim.h"
#include "spi_bus.h"
#include "gpio.h"
#include "lcd.h"
#include "midi.h"

#include "sim_avr.h"
#include "sim_elf.h"
#include "avr_twi.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* ---------------------------------------------------------------------------
 * I2C EEPROM model (24LC1024 at 0x50/0x54)
 *
 * The Nava does NOT use internal AVR EEPROM.  All pattern/config storage goes
 * to an external I2C EEPROM at I2C bus address 0x50 (lower 64 KiB) and 0x54
 * (upper 64 KiB) — selected by WireBeginTX's address comparison against 65535.
 *
 * simavr's TWI IRQ (TWI_IRQ_INPUT) fires when the master AVR writes a byte;
 * we respond to TWI_IRQ_OUTPUT requests with bytes from our image buffer.
 * --------------------------------------------------------------------------- */

/* Optional TWI trace for boot-config diagnosis (R-004).
 * Enable with: make -C sim CFLAGS_EXTRA=-DNAVA_TWI_DEBUG
 * Confirms LoadSeqSetup reads bpm/sync from the seeded EEPROM image. */
#ifdef NAVA_TWI_DEBUG
#define TWI_DBG(ctx, fmt, ...) \
    fprintf(stderr, "[TWI] " fmt "\n", ##__VA_ARGS__)
#else
#define TWI_DBG(ctx, fmt, ...) (void)(ctx)
#endif

/* TWI_COND_* and the avr_twi_msg_irq_t packing come from avr_twi.h — do NOT
 * redeclare them here.  The IRQ payload is a packed bitfield
 * (unused:8, msg:8, addr:8, data:8), so the condition flags live in .msg, the
 * 8-bit wire address (7-bit addr << 1 | R/W) in .addr, and the byte in .data.
 * Modelled on simavr's own examples/parts/i2c_eeprom.c, which cannot be used
 * directly here: it hard-caps at ee[4096] and the Nava needs 128 KiB. */

struct nava_i2c_eeprom_ctx {
    uint8_t     *data;
    size_t       size;
    uint32_t     cur_addr;
    int          addr_upper;   /* 1 = upper 64 KiB (0x54 range) */
    uint8_t      selected;     /* wire address byte if addressed, else 0 */
    int          index;        /* byte counter within the current transaction */
    avr_irq_t   *twi_in_irq;  /* input IRQ for feeding bytes back to AVR */
};

static void twi_irq_cb(struct avr_irq_t *irq, uint32_t value, void *param) {
    struct nava_i2c_eeprom_ctx *ctx = (struct nava_i2c_eeprom_ctx *)param;

    avr_twi_msg_irq_t v;
    v.u.v = value;

    if (v.u.twi.msg & TWI_COND_STOP) {
        TWI_DBG(ctx, "STOP");
        ctx->selected = 0;
        ctx->index    = 0;
    }

    if (v.u.twi.msg & TWI_COND_START) {
        ctx->selected = 0;
        ctx->index    = 0;
        /* Wire address byte: 7-bit 0x50 -> 0xA0, 7-bit 0x54 -> 0xA8.  Match the
         * whole 0xA0-0xAF block so BOTH chip halves select; bit 3 picks the
         * upper 64 KiB.  (Matching on the 7-bit value with mask 0x7C, as this
         * model previously did, could never match 0x54 and silently stranded
         * every upper-half access.) */
        if ((v.u.twi.addr & 0xF0) == 0xA0) {
            ctx->selected   = v.u.twi.addr;
            ctx->addr_upper = (v.u.twi.addr & 0x08) ? 1 : 0;
            TWI_DBG(ctx, "START addr=0x%02X read=%d upper=%d",
                    v.u.twi.addr, v.u.twi.addr & 1, ctx->addr_upper);
            avr_raise_irq(ctx->twi_in_irq,
                          avr_twi_irq_msg(TWI_COND_ACK, ctx->selected, 1));
        }
    }

    if (!ctx->selected) return;

    if (v.u.twi.msg & TWI_COND_WRITE) {
        avr_raise_irq(ctx->twi_in_irq,
                      avr_twi_irq_msg(TWI_COND_ACK, ctx->selected, 1));
        if (ctx->index < 2) {
            /* 24LC1024 takes a 16-bit word address, high byte first
             * (firmware EEprom.ino WireBeginTX: address>>8 then address&0xFF). */
            if (ctx->index == 0)
                ctx->cur_addr = (uint32_t)v.u.twi.data << 8;
            else {
                ctx->cur_addr |= v.u.twi.data;
                if (ctx->addr_upper) ctx->cur_addr += 65536u;
                TWI_DBG(ctx, "SET ADDR 0x%05X", ctx->cur_addr);
            }
        } else {
            if (ctx->cur_addr < ctx->size)
                ctx->data[ctx->cur_addr] = v.u.twi.data;
            ctx->cur_addr++;
        }
        ctx->index++;
    }

    if (v.u.twi.msg & TWI_COND_READ) {
        uint8_t byte = 0xFF;
        if (ctx->cur_addr < ctx->size)
            byte = ctx->data[ctx->cur_addr];
        TWI_DBG(ctx, "READ addr=0x%05X -> 0x%02X", ctx->cur_addr, byte);
        ctx->cur_addr++;
        avr_raise_irq(ctx->twi_in_irq,
                      avr_twi_irq_msg(TWI_COND_READ, ctx->selected, byte));
        ctx->index++;
    }
}

static struct nava_i2c_eeprom_ctx *
i2c_eeprom_create(struct avr_t *avr) {
    struct nava_i2c_eeprom_ctx *ctx = calloc(1, sizeof(*ctx));
    assert(ctx);
    ctx->data = malloc(NAVA_EEPROM_SIZE);
    assert(ctx->data);
    /* Pre-fill with 0xFF — blank device default */
    memset(ctx->data, 0xFF, NAVA_EEPROM_SIZE);
    ctx->size  = NAVA_EEPROM_SIZE;
    ctx->selected = 0;

    /* Hook into TWI0 output IRQ (fires when master writes to bus) */
    avr_irq_t *twi_out = avr_io_getirq(avr, AVR_IOCTL_TWI_GETIRQ(0),
                                         TWI_IRQ_OUTPUT);
    if (twi_out) avr_irq_register_notify(twi_out, twi_irq_cb, ctx);

    /* Retain the input IRQ so our read callback can push bytes back */
    ctx->twi_in_irq = avr_io_getirq(avr, AVR_IOCTL_TWI_GETIRQ(0),
                                      TWI_IRQ_INPUT);
    return ctx;
}

static void i2c_eeprom_destroy(struct nava_i2c_eeprom_ctx *ctx) {
    if (!ctx) return;
    free(ctx->data);
    free(ctx);
}

/* ---------------------------------------------------------------------------
 * nava_sim_create — CI-010
 * --------------------------------------------------------------------------- */
nava_sim_t *nava_sim_create(const char *elf_path) {
    elf_firmware_t fw;
    memset(&fw, 0, sizeof(fw));
    if (elf_read_firmware(elf_path, &fw) != 0) {
        fprintf(stderr, "nava_sim: cannot read ELF: %s\n", elf_path);
        return NULL;
    }

    /* simavr's core-registry name is independent of the ELF's embedded mmcu
     * string; the two identifiers do not need to match textually.  Probe both
     * known spellings and use whichever the LINKED simavr build actually
     * registers, preferring "atmega1284" (the name the original comment
     * asserted).  Update this comment once a build confirms the correct name. */
    struct avr_t *avr = avr_make_mcu_by_name("atmega1284");
    if (!avr) {
        avr = avr_make_mcu_by_name("atmega1284p");
        if (avr)
            fprintf(stderr, "nava_sim: note: registry name is \"atmega1284p\" "
                    "(update comment to record confirmed name)\n");
    }
    if (!avr) {
        fprintf(stderr, "nava_sim: avr_make_mcu_by_name failed for both "
                "\"atmega1284\" and \"atmega1284p\"\n");
        return NULL;
    }
    avr->frequency = 16000000;
    avr_init(avr);
    avr_load_firmware(avr, &fw);

    nava_sim_t *ctx = calloc(1, sizeof(*ctx));
    assert(ctx);
    ctx->avr = avr;
    event_log_init(&ctx->log);

    /* Peripheral models attached in dependency order */
    ctx->eeprom = i2c_eeprom_create(avr);
    ctx->spi    = nava_spi_attach(avr, &ctx->log, ctx->panel_latch);
    ctx->gpio   = nava_gpio_attach(avr, &ctx->log, ctx->spi);
    ctx->lcd    = nava_lcd_attach(avr, &ctx->log);
    ctx->midi   = nava_midi_attach(avr, &ctx->log);

    return ctx;
}

void nava_sim_destroy(nava_sim_t *ctx) {
    if (!ctx) return;
    nava_midi_detach(ctx->midi);
    nava_lcd_detach(ctx->lcd);
    nava_gpio_detach(ctx->gpio);
    nava_spi_detach(ctx->spi);
    i2c_eeprom_destroy(ctx->eeprom);
    event_log_free(&ctx->log);
    free(ctx);
}

/* ---------------------------------------------------------------------------
 * nava_sim_seed_eeprom — CI-011
 * Copies the caller-supplied image into the I2C EEPROM backing store.
 * Must be called BEFORE nava_sim_run_cycles so the firmware's first I2C read
 * (LoadSeqSetup at boot) returns bpm=120, sync=MASTER.
 * --------------------------------------------------------------------------- */
void nava_sim_seed_eeprom(nava_sim_t *ctx,
                           const uint8_t *image, size_t length) {
    assert(ctx && ctx->eeprom);
    size_t n = length < ctx->eeprom->size ? length : ctx->eeprom->size;
    memcpy(ctx->eeprom->data, image, n);
}

/* ---------------------------------------------------------------------------
 * nava_sim_read_eeprom
 * Reads the I2C EEPROM backing store directly, without going through the
 * firmware.  The SysEx restore test needs this: reading a record back through
 * the protocol would share SysexRecordAddress() with the write that produced it,
 * so a record stored at the wrong address would still read back correctly and
 * the test would pass on a corrupted EEPROM.
 * --------------------------------------------------------------------------- */
void nava_sim_read_eeprom(const nava_sim_t *ctx, size_t offset,
                           uint8_t *buf, size_t length) {
    assert(ctx && ctx->eeprom);
    assert(offset + length <= ctx->eeprom->size);
    memcpy(buf, ctx->eeprom->data + offset, length);
}

/* ---------------------------------------------------------------------------
 * nava_sim_run_cycles — CI-012
 * Steps the AVR one instruction at a time until avr->cycle advances by n.
 * Surfaces CPU halt/crash as a return value of 0 rather than hanging.
 * --------------------------------------------------------------------------- */
uint64_t nava_sim_run_cycles(nava_sim_t *ctx, uint64_t n) {
    struct avr_t *avr = ctx->avr;
    uint64_t target   = avr->cycle + n;

    while (avr->cycle < target) {
        int state = avr_run(avr);
        if (state == cpu_Done || state == cpu_Crashed) {
            fprintf(stderr,
                    "nava_sim: AVR halted (state=%d) at cycle %llu\n",
                    state, (unsigned long long)avr->cycle);
            return 0;
        }
        /* cpu_Sleeping is normal during SLEEP instructions in the idle loop;
         * advancing the cycle counter lets timers fire and wake the CPU. */
    }
    return avr->cycle;
}

uint64_t nava_sim_run_until(nava_sim_t *ctx, uint64_t max_cycles,
                             bool (*pred)(nava_sim_t *, void *), void *user) {
    struct avr_t *avr = ctx->avr;
    uint64_t start    = avr->cycle;
    uint64_t target   = start + max_cycles;

    while (avr->cycle < target) {
        if (pred && pred(ctx, user)) break;
        int state = avr_run(avr);
        if (state == cpu_Done || state == cpu_Crashed) break;
    }
    return avr->cycle - start;
}

void nava_sim_panel_set_bit(nava_sim_t *ctx,
                             int byte_idx, int bit_idx, int level) {
    assert(byte_idx >= 0 && byte_idx < 5);
    assert(bit_idx  >= 0 && bit_idx  < 8);
    if (level)
        ctx->panel_latch[byte_idx] |=  (uint8_t)(1u << bit_idx);
    else
        ctx->panel_latch[byte_idx] &= ~(uint8_t)(1u << bit_idx);
}
