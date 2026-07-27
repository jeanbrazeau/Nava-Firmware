/* sim/harness/event_log.h
 * CI-013: Cycle-stamped, typed event log shared by all peripheral models.
 * Every observable the test assertions read lives here; nothing is asserted
 * on wall-clock time. */
#ifndef NAVA_EVENT_LOG_H
#define NAVA_EVENT_LOG_H

#include <stdint.h>
#include <stddef.h>

typedef uint64_t avr_cycle_t;

typedef enum {
    EVT_NONE = 0,
    EVT_TRIG_WORD,      /* SetDoutTrig: 16-bit trigger word                */
    EVT_LED_WRITE,      /* SetDoutLed: 5-byte chain (stepLed hi, stepLed lo,
                           configLed hi, configLed lo, menuLed)             */
    EVT_DAC_WRITE,      /* SetDacA decoded: channel (0=A) + velocity        */
    EVT_MUX_ATTR,       /* voice attribution sampled at DAC_CS deassert     */
    EVT_MIDI_TX_BYTE,   /* raw USART1 TX byte before decoding               */
    EVT_MIDI_NOTE_ON,   /* decoded channel-message: ch, note (wire), vel    */
    EVT_MIDI_NOTE_OFF,  /* decoded channel-message: ch, note (wire)         */
    EVT_MIDI_RT,        /* real-time byte (0xF8 clock, 0xFA start, etc.)    */
    EVT_LCD_CHAR,       /* HD44780 DDRAM write: row, col, ASCII char        */
    EVT_LCD_CMD,        /* HD44780 command byte                             */
    EVT_DIN_CLK,        /* PORTA1 (DIN clock) edge: level 0/1              */
    EVT_TRIG_OUT,       /* PORTA2 (TRIG_OUT) edge: level 0/1               */
} event_type_t;

typedef struct {
    avr_cycle_t  cycle;
    event_type_t type;
    union {
        uint16_t trig_word;
        struct { uint8_t bytes[5]; }       led;
        struct { uint8_t channel; uint8_t velocity; } dac;
        struct { uint8_t voice;   uint8_t velocity; } mux;
        uint8_t  raw_byte;                    /* EVT_MIDI_TX_BYTE / EVT_MIDI_RT */
        struct { uint8_t channel; uint8_t note; uint8_t velocity; } midi_note;
        struct { uint8_t row; uint8_t col; char ch; } lcd_char;
        uint8_t  lcd_cmd;
        uint8_t  level;                       /* EVT_DIN_CLK / EVT_TRIG_OUT */
    };
} sim_event_t;

typedef struct {
    sim_event_t *buf;
    size_t       count;
    size_t       capacity;
} event_log_t;

void event_log_init(event_log_t *log);
void event_log_free(event_log_t *log);
void event_log_append(event_log_t *log, const sim_event_t *evt);
void event_log_clear(event_log_t *log);

/* Query helpers — cycle_hi==0 means unbounded upper limit */
const sim_event_t *event_log_find_first(const event_log_t *log,
                                         event_type_t type,
                                         avr_cycle_t cycle_lo,
                                         avr_cycle_t cycle_hi);
const sim_event_t *event_log_find_nth(const event_log_t *log,
                                       event_type_t type,
                                       avr_cycle_t cycle_lo,
                                       avr_cycle_t cycle_hi,
                                       size_t n);           /* 0-based */
size_t event_log_count_type(const event_log_t *log,
                             event_type_t type,
                             avr_cycle_t cycle_lo,
                             avr_cycle_t cycle_hi);
void event_log_foreach(const event_log_t *log,
                        event_type_t type,
                        avr_cycle_t cycle_lo,
                        avr_cycle_t cycle_hi,
                        void (*fn)(const sim_event_t *, void *),
                        void *user);

#endif /* NAVA_EVENT_LOG_H */
