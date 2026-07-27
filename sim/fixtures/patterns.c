/* sim/fixtures/patterns.c — CI-071
 * Pattern fixture data and EEPROM serialization helpers. */
#include "patterns.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ---- EEPROM serialization (mirrors SavePattern write order) ---- */

void fx_pattern_to_eeprom(const fx_pattern_t *p, uint8_t *dst) {
    uint8_t *d = dst;
    memset(d, 0, FX_PTRN_SIZE);

    /* [0..31]: inst[16] — 2 bytes each, little-endian */
    for (int i = 0; i < 16; i++) {
        d[i * 2 + 0] = (uint8_t)(p->inst[i] & 0xFF);
        d[i * 2 + 1] = (uint8_t)((p->inst[i] >> 8) & 0xFF);
    }

    /* [32..63]: setup block */
    d[32] = p->length;
    d[33] = p->scale;
    d[34] = p->shuffle;
    d[35] = p->flam;
    d[36] = 0;            /* old extLength (unused) */
    d[37] = p->groupPos;
    d[38] = p->groupLength;
    d[39] = p->totalAcc;
    /* d[40..63] = 24 unused parameter bytes (zeroed by memset) */

    /* [64..95]: extTrack[16] — 2 bytes each, little-endian */
    for (int i = 0; i < 16; i++) {
        d[64 + i * 2 + 0] = (uint8_t)(p->extTrack[i] & 0xFF);
        d[64 + i * 2 + 1] = (uint8_t)((p->extTrack[i] >> 8) & 0xFF);
    }
    /* [96..127] = 32 bytes padding (zeroed) */
    /* [128..191] = second compat page (zeroed) */

    /* [192..447]: velocity[16][16] — 256 bytes in instrument-major order */
    for (int inst = 0; inst < 16; inst++) {
        for (int step = 0; step < 16; step++) {
            d[192 + inst * 16 + step] = p->velocity[inst][step];
        }
    }
}

uint8_t *fx_make_eeprom_image(const fx_pattern_t *pattern0,
                               uint8_t ext_channel,
                               uint8_t ptrn_change_sync) {
    uint8_t *img = malloc(FX_EEPROM_SIZE);
    assert(img);
    /* Blank EEPROM = 0xFF */
    memset(img, 0xFF, FX_EEPROM_SIZE);

    /* Pattern slot 0: caller-supplied pattern */
    if (pattern0) {
        fx_pattern_to_eeprom(pattern0, img);
    }

    /* Remaining pattern slots (1..127): default fixture */
    for (size_t n = 1; n < FX_MAX_PTRN; n++) {
        fx_pattern_to_eeprom(&FX_PTRN_BASIC, img + n * FX_PTRN_SIZE);
    }

    /* Track bank: leave as 0xFF (END_OF_TRACK = 128; 0xFF > 128 is also end) */

    /* Setup block at OFFSET_SETUP */
    uint8_t *setup = img + FX_OFFSET_SETUP;
    memset(setup, 0, 64);
    setup[0] = 0;               /* sync = MASTER */
    setup[1] = 120;             /* defaultBpm */
    setup[2] = 1;               /* TXchannel */
    setup[3] = 1;               /* RXchannel */
    setup[4] = ptrn_change_sync;/* ptrnChangeSync (1=SYNC, 0=FREE) */
    setup[5] = 1;               /* muteModeHH */
    setup[6] = ext_channel;     /* EXTchannel */
    setup[7] = 3;               /* BootMode = PTRN_STEP */

    return img;
}

/* ---- Pre-built fixture instances ---- */

/* Basic: all 16 BD steps active; velocity = instVelHigh[BD]=50;
 * shuffle=1 to stay safely away from OOB. */
const fx_pattern_t FX_PTRN_BASIC = {
    .length  = 15,
    .scale   = FX_SCALE_16,
    .shuffle = 1,
    .flam    = 0,
    .inst    = {
        /* index = bit position; BD=8, so inst[BD]=0xFFFF means all steps */
        [FX_BD] = 0xFFFFu
    },
    .velocity = {
        /* velocity[FX_BD][step] = instVelHigh[BD] = 50 for all 16 steps */
        [FX_BD] = {50,50,50,50, 50,50,50,50, 50,50,50,50, 50,50,50,50},
    },
};

/* Shuffled: BD on beats 0,4,8,12; SD on beats 4,12; shuffle=2 */
const fx_pattern_t FX_PTRN_SHUFFLED = {
    .length  = 15,
    .scale   = FX_SCALE_16,
    .shuffle = 2,
    .flam    = 0,
    .inst    = {
        /* BD on EVERY step.  The shuffle offset is applied to ODD steps only,
         * so a fixture that triggers just steps 0,4,8,12 (all even) produces
         * no observable shuffle at all — successive onsets would sit a clean
         * 4 step periods apart and the test could never see the swing. */
        [FX_BD] = 0xFFFFu,
    },
    .velocity = {
        [FX_BD] = {50,50,50,50, 50,50,50,50, 50,50,50,50, 50,50,50,50},
    },
};

/* Flam: BD on steps 0,4 with flam type 0 (20 ms delayed hit).
 * Velocity bit 7 = flam flag; lower 7 bits = actual velocity. */
const fx_pattern_t FX_PTRN_FLAM = {
    .length  = 15,
    .scale   = FX_SCALE_16,
    .shuffle = 1,
    .flam    = 0,   /* OCR3A = flam[0] = 4999; period = 5000*64 = 320000 cycles */
    .inst    = {
        [FX_BD] = (1u<<0)|(1u<<4),
    },
    .velocity = {
        /* bit7=1 = flam flag; lower 7 bits = velocity */
        [FX_BD] = {50|128, 0, 0, 0,  50|128, 0, 0, 0,  0,0,0,0,  0,0,0,0},
    },
};

/* EXT_INST: extTrack[0] on steps 0,8; extTrack[3] on steps 0,8;
 * accent (TOTAL_ACC) on step 8 to exercise the MIDI_ACCENT_VELOCITY=16 bug.
 * EXT_INST velocity field used for per-step velocity (mapped via MIDI_EXT_CHANNEL). */
const fx_pattern_t FX_PTRN_EXT = {
    .length  = 15,
    .scale   = FX_SCALE_16,
    .shuffle = 1,
    .flam    = 0,
    .inst    = {
        /* TOTAL_ACC = 12: accent on step 8 */
        [FX_TOTAL_ACC] = (1u<<8),
        /* EXT_INST = 13: required so Clock.ino's EXT trigger code fires */
        [FX_EXT_INST]  = (1u<<0)|(1u<<8),
    },
    .extTrack = {
        /* extTrack[0]: steps 0 and 8 */
        [0] = (1u<<0)|(1u<<8),
        /* extTrack[3]: steps 0 and 8 */
        [3] = (1u<<0)|(1u<<8),
    },
    .velocity = {
        /* EXT_INST velocity = instVelHigh[EXT_INST]=50 for step 0;
         * step 8 will use MIDI_ACCENT_VELOCITY=16 due to TOTAL_ACC bit. */
        [FX_EXT_INST] = {50,0,0,0, 0,0,0,0, 50,0,0,0, 0,0,0,0},
    },
};

/* Trigger-vs-MIDI alignment fixture: BD fires the analog trigger inline in
 * CountPPQN() while ext tracks 0 and 3 are queued for the loop to transmit, so
 * every step yields a matched (TRIG_WORD, NOTE_ON) pair to difference. */
const fx_pattern_t FX_PTRN_EXT_SYNC = {
    .length  = 15,
    .scale   = FX_SCALE_16,
    .shuffle = 1,
    .flam    = 0,
    .inst    = { [FX_BD] = 0xFFFFu },
    .extTrack = {
        [0] = 0xFFFFu,
        [3] = 0xFFFFu,
    },
    .velocity = {
        [FX_BD]       = {50,50,50,50, 50,50,50,50, 50,50,50,50, 50,50,50,50},
        [FX_EXT_INST] = {50,50,50,50, 50,50,50,50, 50,50,50,50, 50,50,50,50},
    },
};

/* All 16 ext tracks on every step: 16 note-offs + 16 note-ons is more than the 64 byte
 * UART TX ring can absorb, so this is the case the clock-side transmit must decline and
 * leave for the loop.  Exercises that fallback rather than assuming it. */
const fx_pattern_t FX_PTRN_EXT_DENSE = {
    .length  = 15,
    .scale   = FX_SCALE_16,
    .shuffle = 1,
    .flam    = 0,
    .inst    = { [FX_BD] = 0xFFFFu },
    .extTrack = {
        0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu,
        0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu,
    },
    .velocity = {
        [FX_BD]       = {50,50,50,50, 50,50,50,50, 50,50,50,50, 50,50,50,50},
        [FX_EXT_INST] = {50,50,50,50, 50,50,50,50, 50,50,50,50, 50,50,50,50},
    },
};

/* Pattern swap group: Group A triggers BD on all steps (trig word bit BD=8 set) */
const fx_pattern_t FX_PTRN_GROUP_A = {
    .length  = 15,
    .scale   = FX_SCALE_16,
    .shuffle = 1,
    .flam    = 0,
    .inst    = { [FX_BD] = 0xFFFFu },
    .velocity = {
        [FX_BD] = {50,50,50,50, 50,50,50,50, 50,50,50,50, 50,50,50,50},
    },
};

/* Pattern swap group: Group B triggers SD on all steps (different trig word) */
const fx_pattern_t FX_PTRN_GROUP_B = {
    .length  = 15,
    .scale   = FX_SCALE_16,
    .shuffle = 1,
    .flam    = 0,
    .inst    = { [FX_SD] = 0xFFFFu },
    .velocity = {
        [FX_SD] = {50,50,50,50, 50,50,50,50, 50,50,50,50, 50,50,50,50},
    },
};
