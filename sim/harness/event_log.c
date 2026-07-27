/* sim/harness/event_log.c  — CI-013 */
#include "event_log.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define INITIAL_CAP 4096

void event_log_init(event_log_t *log) {
    log->buf      = malloc(INITIAL_CAP * sizeof(sim_event_t));
    assert(log->buf);
    log->count    = 0;
    log->capacity = INITIAL_CAP;
}

void event_log_free(event_log_t *log) {
    free(log->buf);
    log->buf      = NULL;
    log->count    = 0;
    log->capacity = 0;
}

void event_log_append(event_log_t *log, const sim_event_t *evt) {
    if (log->count == log->capacity) {
        log->capacity *= 2;
        log->buf = realloc(log->buf, log->capacity * sizeof(sim_event_t));
        assert(log->buf);
    }
    log->buf[log->count++] = *evt;
}

void event_log_clear(event_log_t *log) {
    log->count = 0;
}

static int in_window(const sim_event_t *e, event_type_t t,
                     avr_cycle_t lo, avr_cycle_t hi) {
    return (e->type == t) &&
           (e->cycle >= lo) &&
           (hi == 0 || e->cycle <= hi);
}

const sim_event_t *event_log_find_first(const event_log_t *log,
                                         event_type_t type,
                                         avr_cycle_t lo, avr_cycle_t hi) {
    for (size_t i = 0; i < log->count; i++) {
        if (in_window(&log->buf[i], type, lo, hi)) return &log->buf[i];
    }
    return NULL;
}

const sim_event_t *event_log_find_nth(const event_log_t *log,
                                       event_type_t type,
                                       avr_cycle_t lo, avr_cycle_t hi,
                                       size_t n) {
    size_t found = 0;
    for (size_t i = 0; i < log->count; i++) {
        if (in_window(&log->buf[i], type, lo, hi)) {
            if (found == n) return &log->buf[i];
            found++;
        }
    }
    return NULL;
}

size_t event_log_count_type(const event_log_t *log,
                             event_type_t type,
                             avr_cycle_t lo, avr_cycle_t hi) {
    size_t n = 0;
    for (size_t i = 0; i < log->count; i++) {
        if (in_window(&log->buf[i], type, lo, hi)) n++;
    }
    return n;
}

void event_log_foreach(const event_log_t *log,
                        event_type_t type,
                        avr_cycle_t lo, avr_cycle_t hi,
                        void (*fn)(const sim_event_t *, void *),
                        void *user) {
    for (size_t i = 0; i < log->count; i++) {
        if (in_window(&log->buf[i], type, lo, hi)) fn(&log->buf[i], user);
    }
}
