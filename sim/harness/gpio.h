/* sim/harness/gpio.h
 * CI-030..CI-033: MUX voice attribution, DIN/TRIG_OUT logging, encoder
 * quadrature injection, encoder switch. */
#ifndef NAVA_GPIO_H
#define NAVA_GPIO_H

#include <stdint.h>
#include "event_log.h"
#include "spi_bus.h"

struct avr_t;
typedef struct nava_gpio_ctx nava_gpio_ctx_t;

/* Voice indices matching firmware's muxInst[] = {LT,SD,BD,MT,HT,HC,RM,CH,CRASH,RIDE} */
typedef enum {
    VOICE_LT=0, VOICE_SD, VOICE_BD, VOICE_MT, VOICE_HT,
    VOICE_HC,   VOICE_RM, VOICE_CH, VOICE_CRASH, VOICE_RIDE,
    VOICE_COUNT
} voice_t;

const char *voice_name(voice_t v);

nava_gpio_ctx_t *nava_gpio_attach(struct avr_t *avr, event_log_t *log,
                                   nava_spi_ctx_t *spi);
void nava_gpio_detach(nava_gpio_ctx_t *ctx);

/* Inject Gray-code quadrature on PINB0/1.
 * direction: +1=CW, -1=CCW.  detents: number of encoder clicks.
 * Each click produces 2 edge-pairs with ~3200-cycle spacing (≈200 µs).
 * Advances the simulation clock internally to space the edges. */
void nava_gpio_inject_encoder(nava_gpio_ctx_t *ctx,
                               int direction, int detents);

/* Drive PINB2 (encoder switch) to pressed (1) or released (0). */
void nava_gpio_set_encoder_switch(nava_gpio_ctx_t *ctx, int pressed);

#endif /* NAVA_GPIO_H */
