/* sim/tests/test_sysex.c
 * SysEx pattern/track/setup transfer (Midi.ino).
 *
 * Firmware facts this pins:
 *   - The handler is only connected on config page 3, reached with SHIFT+TEMPO
 *     three times while stopped (Seq.ino: first press enters config mode at page
 *     1, each further press increments).  Off that page the callback is
 *     disconnected and requests must be ignored entirely.
 *   - Records are the EEPROM images verbatim - 448 bytes for a pattern, 1024 for
 *     a track, 64 for the setup block - 7-in-8 packed, with a checksum over the
 *     RAW bytes.
 *   - An incoming record is checksummed before any of it is written, so a
 *     corrupt transfer must leave the stored pattern untouched.
 *
 * Writes are verified by reading the EEPROM backing store directly rather than
 * by requesting the record back: a read-back shares SysexRecordAddress() with
 * the write, so a record stored at the wrong address would still round-trip and
 * the test would pass on a corrupted EEPROM.
 */
#include "test_runner.h"
#include "frontpanel.h"
#include "event_log.h"
#include "nava_sim.h"
#include "midi.h"
#include "patterns.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOOT_CYCLES 64000000ULL

/* Sysex.h */
#define SX_MANUFACTURER 0x7Du
#define SX_DEVID_1      0x07u
#define SX_DEVID_2      0x1Au
#define SX_HEADERSIZE   6u

#define SX_PTRN_DMP     0x01u
#define SX_TRACK_DMP    0x02u
#define SX_CONFIG_DMP   0x03u
#define SX_BANK_REQ     0x40u
#define SX_PTRN_REQ     0x41u
#define SX_TRACK_REQ    0x42u
#define SX_CONFIG_REQ   0x43u
#define SX_ACK          0x48u

#define SX_ACK_OK           0u
#define SX_ACK_BAD_CHECKSUM 1u
#define SX_ACK_BAD_PARAM    3u

#define PTRN_BYTES 448u

/* A dump is 520 bytes at 320 us/byte plus the EEPROM reads behind it; the
 * firmware also has to notice the message in its polling loop.  Generous, since
 * the assertions are on content rather than timing. */
#define REPLY_CYCLES 60000000ULL
#define ACK_CYCLES   40000000ULL

/* ---- 7-in-8 packing, mirroring tools/nava/protocol.py ---- */

static size_t sx_pack(const uint8_t *raw, size_t len, uint8_t *out) {
    size_t o = 0;
    for (size_t i = 0; i < len; i += 7) {
        size_t n = (len - i) < 7 ? (len - i) : 7;
        uint8_t msbs = 0;
        for (size_t k = 0; k < n; k++) msbs |= (uint8_t)((raw[i + k] >> 7) << k);
        out[o++] = msbs;
        for (size_t k = 0; k < n; k++) out[o++] = raw[i + k] & 0x7F;
    }
    return o;
}

static size_t sx_unpack(const uint8_t *packed, size_t len, uint8_t *out) {
    size_t o = 0;
    for (size_t i = 0; i < len; i += 8) {
        size_t n = (len - i) < 8 ? (len - i) : 8;
        if (n < 2) break;
        uint8_t msbs = packed[i];
        for (size_t k = 1; k < n; k++)
            out[o++] = (uint8_t)(packed[i + k] | (uint8_t)(((msbs >> (k - 1)) & 1) << 7));
    }
    return o;
}

static uint8_t sx_checksum(const uint8_t *raw, size_t len) {
    unsigned sum = 0;
    for (size_t i = 0; i < len; i++) sum += raw[i];
    return (uint8_t)(sum & 0x7F);
}

/* Build a complete F0..F7 message.  Returns its length. */
static size_t sx_message(uint8_t cmd, uint8_t param,
                         const uint8_t *payload, size_t len, uint8_t *out) {
    size_t o = 0;
    out[o++] = 0xF0;
    out[o++] = SX_MANUFACTURER;
    out[o++] = SX_DEVID_1;
    out[o++] = SX_DEVID_2;
    out[o++] = cmd;
    out[o++] = param;
    o += sx_pack(payload, len, out + o);
    out[o++] = sx_checksum(payload, len);
    out[o++] = 0xF7;
    return o;
}

/* ---- Capturing the reply ---- */

typedef struct {
    uint8_t buf[2048];
    size_t  len;
    int     in_message;
    int     complete;
} sx_capture_t;

static void sx_collect(const sim_event_t *evt, void *user) {
    sx_capture_t *cap = (sx_capture_t *)user;
    uint8_t byte = evt->raw_byte;

    if (cap->complete) return;

    /* Real-time bytes are legal ANYWHERE, including between two data bytes of a
     * SysEx message, and a receiver is required to strip them. The Nava emits
     * MIDI clock continuously in MASTER sync - Timer1 runs whether or not the
     * sequencer is started - so a dump always arrives with 0xF8 sprinkled
     * through it. Keeping them here corrupts the payload and the checksum. */
    if (byte >= 0xF8u) return;

    if (byte == 0xF0) {
        cap->in_message = 1;
        cap->len = 0;
    }
    if (!cap->in_message) return;
    if (cap->len < sizeof(cap->buf)) cap->buf[cap->len++] = byte;
    if (byte == 0xF7 && cap->len > 1) {
        cap->in_message = 0;
        /* An empty F0 F7 is the running-status reset SysexResetRunningStatus()
         * emits after every message; it is not a reply and must be skipped. */
        if (cap->len > 2) cap->complete = 1;
        else cap->len = 0;
    }
}

/* Collect the first complete SysEx message the firmware transmitted after
 * cycle_lo.  Returns 0 if none. */
static int sx_capture(const nava_sim_t *ctx, uint64_t cycle_lo, sx_capture_t *cap) {
    memset(cap, 0, sizeof(*cap));
    event_log_foreach(&ctx->log, EVT_MIDI_TX_BYTE, cycle_lo, 0, sx_collect, cap);
    return cap->complete;
}

/* Validate the frame and hand back the decoded payload. */
static int sx_decode(const sx_capture_t *cap, const char *label,
                     uint8_t *cmd, uint8_t *param, uint8_t *payload, size_t *payload_len) {
    if (cap->len < SX_HEADERSIZE + 2) {
        test_fail(label, "reply is %zu bytes, too short to be a message", cap->len);
        return 0;
    }
    if (cap->buf[1] != SX_MANUFACTURER || cap->buf[2] != SX_DEVID_1 ||
        cap->buf[3] != SX_DEVID_2) {
        test_fail(label, "reply header is %02X %02X %02X, expected 7D 07 1A",
                  cap->buf[1], cap->buf[2], cap->buf[3]);
        return 0;
    }
    *cmd = cap->buf[4];
    *param = cap->buf[5];

    size_t packed_len = cap->len - SX_HEADERSIZE - 2;
    *payload_len = sx_unpack(cap->buf + SX_HEADERSIZE, packed_len, payload);

    uint8_t want = cap->buf[cap->len - 2];
    uint8_t got = sx_checksum(payload, *payload_len);
    if (got != want) {
        fprintf(stderr,
                "# %s: message %zu bytes, packed %zu, unpacked %zu\n"
                "#   head: %02X %02X %02X %02X %02X %02X %02X %02X\n"
                "#   tail: %02X %02X %02X %02X\n",
                label, cap->len, packed_len, *payload_len,
                cap->buf[0], cap->buf[1], cap->buf[2], cap->buf[3],
                cap->buf[4], cap->buf[5], cap->buf[6], cap->buf[7],
                cap->buf[cap->len - 4], cap->buf[cap->len - 3],
                cap->buf[cap->len - 2], cap->buf[cap->len - 1]);
        test_fail(label, "checksum mismatch: computed %02X, message carries %02X", got, want);
        return 0;
    }
    for (size_t i = 1; i + 1 < cap->len; i++) {
        if (cap->buf[i] > 0x7F) {
            test_fail(label, "byte %zu is %02X: bit 7 set inside a SysEx message",
                      i, cap->buf[i]);
            return 0;
        }
    }
    return 1;
}

/* ---- Navigation ---- */

/* SHIFT + TEMPO n times, stopped, lands on config page n.
 *
 * The settle after pressing SHIFT is load-bearing: the page handler sits inside
 * `if (shiftBtn)` (Seq.ino) and reads a debounced level, so a TEMPO edge scanned
 * in the same pass as the SHIFT press is dropped and the walk ends one page
 * short. */
static void goto_config_page(nava_sim_t *ctx, int page) {
    fp_press_button(ctx, FP_BTN_SHIFT);
    fp_settle(ctx);
    for (int i = 0; i < page; i++) {
        fp_press_button(ctx, FP_BTN_TEMPO);
        fp_release_button(ctx, FP_BTN_TEMPO);
        fp_settle(ctx);
    }
    fp_release_button(ctx, FP_BTN_SHIFT);
    fp_settle(ctx);
}

static void enter_sysex_page(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    goto_config_page(ctx, FX_CONF_PAGE_SYSEX);
    /* LCD.ino prints "type    select  " on the SysEx page; if this fails the
     * page numbering moved and every assertion below would be meaningless. */
    assert_lcd_contains("sysex page reached", ctx, 0, "Type");
}

static void inject(nava_sim_t *ctx, const uint8_t *msg, size_t len) {
    nava_midi_inject_bytes(ctx->midi, msg, len);
}

/* ---- Tests ---- */

/* Pattern 1 rather than 0: fx_make_eeprom_image() leaves slot 0 blank (0xFF) and
 * fills 1..127 with FX_PTRN_BASIC, so slot 1 has real content and differs from
 * its neighbour - which is what lets this test tell a correct read from one off
 * by a record. */
#define REQ_PTRN 1u

static void test_pattern_request_returns_the_eeprom_record(nava_sim_t *ctx) {
    enter_sysex_page(ctx);
    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;

    uint8_t request[16];
    size_t request_len = sx_message(SX_PTRN_REQ, REQ_PTRN, NULL, 0, request);
    inject(ctx, request, request_len);
    nava_sim_run_cycles(ctx, REPLY_CYCLES);

    sx_capture_t cap;
    if (!sx_capture(ctx, t0, &cap)) {
        test_fail("pattern request", "no complete SysEx reply was transmitted");
        return;
    }

    uint8_t cmd, param, payload[2048];
    size_t payload_len;
    if (!sx_decode(&cap, "pattern request", &cmd, &param, payload, &payload_len)) return;

    if (cmd != SX_PTRN_DMP)
        test_fail("pattern request", "replied with command %02X, expected a pattern dump", cmd);
    if (param != REQ_PTRN)
        test_fail("pattern request", "replied for pattern %u, expected %u", param, REQ_PTRN);
    if (payload_len != PTRN_BYTES) {
        test_fail("pattern request", "payload is %zu bytes, expected %u",
                  payload_len, PTRN_BYTES);
        return;
    }
    if (cap.len != 520)
        test_fail("pattern request", "message is %zu bytes, expected 520", cap.len);

    /* Compared against the EEPROM as it stands now, not against the fixture:
     * the firmware may legitimately have rewritten a record since boot. */
    uint8_t expected[PTRN_BYTES], neighbour[PTRN_BYTES];
    nava_sim_read_eeprom(ctx, REQ_PTRN * FX_PTRN_SIZE, expected, sizeof(expected));
    nava_sim_read_eeprom(ctx, (REQ_PTRN - 1) * FX_PTRN_SIZE, neighbour, sizeof(neighbour));
    if (memcmp(expected, neighbour, PTRN_BYTES) == 0) {
        test_fail("pattern request",
                  "slots %u and %u are identical, so this test cannot detect a "
                  "misaddressed read", REQ_PTRN - 1, REQ_PTRN);
        return;
    }
    if (memcmp(payload, expected, PTRN_BYTES) != 0) {
        for (size_t i = 0; i < PTRN_BYTES; i++) {
            if (payload[i] != expected[i]) {
                test_fail("pattern request",
                          "payload differs from EEPROM at byte %zu: got %02X, expected %02X",
                          i, payload[i], expected[i]);
                break;
            }
        }
    }
}

static void test_config_request_returns_the_setup_record(nava_sim_t *ctx) {
    enter_sysex_page(ctx);
    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;

    uint8_t request[16];
    inject(ctx, request, sx_message(SX_CONFIG_REQ, 0, NULL, 0, request));
    nava_sim_run_cycles(ctx, REPLY_CYCLES);

    sx_capture_t cap;
    if (!sx_capture(ctx, t0, &cap)) {
        test_fail("config request", "no reply");
        return;
    }
    uint8_t cmd, param, payload[2048];
    size_t payload_len;
    if (!sx_decode(&cap, "config request", &cmd, &param, payload, &payload_len)) return;

    if (cmd != SX_CONFIG_DMP)
        test_fail("config request", "replied with command %02X", cmd);
    if (payload_len != 64)
        test_fail("config request", "payload is %zu bytes, expected 64", payload_len);

    uint8_t expected[64];
    nava_sim_read_eeprom(ctx, FX_OFFSET_SETUP, expected, sizeof(expected));
    if (memcmp(payload, expected, sizeof(expected)) != 0)
        test_fail("config request", "setup payload does not match EEPROM");
}

static void test_pattern_dump_is_written_and_acknowledged(nava_sim_t *ctx) {
    enter_sysex_page(ctx);

    /* A record with bit 7 set in every byte: a packing bug that drops high bits
     * would leave the EEPROM full of 0x0n and be caught byte for byte. */
    uint8_t record[PTRN_BYTES];
    for (size_t i = 0; i < PTRN_BYTES; i++) record[i] = (uint8_t)((i * 31 + 5) | 0x80);

    static uint8_t msg[1024];
    size_t msg_len = sx_message(SX_PTRN_DMP, 5, record, PTRN_BYTES, msg);

    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;
    inject(ctx, msg, msg_len);
    nava_sim_run_cycles(ctx, ACK_CYCLES);

    sx_capture_t cap;
    if (!sx_capture(ctx, t0, &cap)) {
        test_fail("pattern dump", "device sent no acknowledgement");
        return;
    }
    uint8_t cmd, param, payload[2048];
    size_t payload_len;
    if (!sx_decode(&cap, "pattern dump", &cmd, &param, payload, &payload_len)) return;

    if (cmd != SX_ACK)
        test_fail("pattern dump", "replied with command %02X, expected an ack", cmd);
    else if (param != SX_ACK_OK)
        test_fail("pattern dump", "ack status %u, expected 0 (ok)", param);

    /* Read the EEPROM directly - see the file header. */
    uint8_t stored[PTRN_BYTES];
    nava_sim_read_eeprom(ctx, 5u * FX_PTRN_SIZE, stored, sizeof(stored));
    if (memcmp(stored, record, PTRN_BYTES) != 0) {
        for (size_t i = 0; i < PTRN_BYTES; i++) {
            if (stored[i] != record[i]) {
                test_fail("pattern dump",
                          "EEPROM byte %zu is %02X, expected %02X",
                          i, stored[i], record[i]);
                break;
            }
        }
    }
}

static void test_corrupt_dump_is_rejected_and_changes_nothing(nava_sim_t *ctx) {
    enter_sysex_page(ctx);

    uint8_t before[PTRN_BYTES];
    nava_sim_read_eeprom(ctx, 7u * FX_PTRN_SIZE, before, sizeof(before));

    uint8_t record[PTRN_BYTES];
    for (size_t i = 0; i < PTRN_BYTES; i++) record[i] = (uint8_t)(i ^ 0xA5);

    static uint8_t msg[1024];
    size_t msg_len = sx_message(SX_PTRN_DMP, 7, record, PTRN_BYTES, msg);
    msg[20] ^= 0x01;   /* one flipped payload bit */

    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;
    inject(ctx, msg, msg_len);
    nava_sim_run_cycles(ctx, ACK_CYCLES);

    sx_capture_t cap;
    if (!sx_capture(ctx, t0, &cap)) {
        test_fail("corrupt dump", "device sent no acknowledgement");
        return;
    }
    if (cap.len < SX_HEADERSIZE + 2) {
        test_fail("corrupt dump", "reply is %zu bytes, too short to be an ack", cap.len);
        return;
    }
    uint8_t cmd = cap.buf[4];
    uint8_t param = cap.buf[5];

    if (cmd != SX_ACK)
        test_fail("corrupt dump", "replied with command %02X, expected an ack", cmd);
    else if (param != SX_ACK_BAD_CHECKSUM)
        test_fail("corrupt dump", "ack status %u, expected %u (bad checksum)",
                  param, SX_ACK_BAD_CHECKSUM);

    uint8_t after[PTRN_BYTES];
    nava_sim_read_eeprom(ctx, 7u * FX_PTRN_SIZE, after, sizeof(after));
    if (memcmp(before, after, PTRN_BYTES) != 0)
        test_fail("corrupt dump", "EEPROM changed despite the checksum failing");
}

static void test_out_of_range_track_is_refused(nava_sim_t *ctx) {
    enter_sysex_page(ctx);
    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;

    uint8_t request[16];
    inject(ctx, request, sx_message(SX_TRACK_REQ, FX_MAX_TRACK, NULL, 0, request));
    nava_sim_run_cycles(ctx, ACK_CYCLES);

    sx_capture_t cap;
    if (!sx_capture(ctx, t0, &cap)) {
        test_fail("bad track", "no reply to an out-of-range track request");
        return;
    }
    if (cap.len < SX_HEADERSIZE + 2) {
        test_fail("bad track", "reply is %zu bytes, too short to be an ack", cap.len);
        return;
    }
    if (cap.buf[4] != SX_ACK)
        test_fail("bad track", "replied with command %02X, expected an ack", cap.buf[4]);
    else if (cap.buf[5] != SX_ACK_BAD_PARAM)
        test_fail("bad track", "ack status %u, expected %u (bad parameter)",
                  cap.buf[5], SX_ACK_BAD_PARAM);
}

static void test_track_request_returns_a_whole_kilobyte(nava_sim_t *ctx) {
    enter_sysex_page(ctx);
    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;

    uint8_t request[16];
    inject(ctx, request, sx_message(SX_TRACK_REQ, 0, NULL, 0, request));
    /* 1179 bytes on the wire is more than twice a pattern dump. */
    nava_sim_run_cycles(ctx, REPLY_CYCLES * 2);

    sx_capture_t cap;
    if (!sx_capture(ctx, t0, &cap)) {
        test_fail("track request", "no reply");
        return;
    }
    uint8_t cmd, param, payload[2048];
    size_t payload_len;
    if (!sx_decode(&cap, "track request", &cmd, &param, payload, &payload_len)) return;

    if (cmd != SX_TRACK_DMP)
        test_fail("track request", "replied with command %02X", cmd);
    if (cap.len != 1179)
        test_fail("track request", "message is %zu bytes, expected 1179", cap.len);
    if (payload_len != FX_TRACK_SIZE) {
        test_fail("track request", "payload is %zu bytes, expected %u",
                  payload_len, FX_TRACK_SIZE);
        return;
    }

    uint8_t expected[FX_TRACK_SIZE];
    nava_sim_read_eeprom(ctx, FX_PTRN_SIZE * FX_MAX_PTRN, expected, sizeof(expected));
    if (memcmp(payload, expected, sizeof(expected)) != 0)
        test_fail("track request", "track payload does not match EEPROM");
}

static void test_requests_are_ignored_off_the_sysex_page(nava_sim_t *ctx) {
    boot_wait_ready(ctx, BOOT_CYCLES);
    /* Page 1 is the ordinary setup page; the handler is disconnected there. */
    goto_config_page(ctx, 1);
    event_log_clear(&ctx->log);
    uint64_t t0 = ctx->avr->cycle;

    uint8_t request[16];
    inject(ctx, request, sx_message(SX_PTRN_REQ, 0, NULL, 0, request));
    nava_sim_run_cycles(ctx, REPLY_CYCLES);

    sx_capture_t cap;
    if (sx_capture(ctx, t0, &cap))
        test_fail("off-page request",
                  "device answered a request outside SysEx mode (%zu bytes)", cap.len);
}

int main(void) {
    TEST("sysex_pattern_request_returns_eeprom_record",
         test_pattern_request_returns_the_eeprom_record);
    TEST("sysex_config_request_returns_setup_record",
         test_config_request_returns_the_setup_record);
    TEST("sysex_track_request_returns_whole_kilobyte",
         test_track_request_returns_a_whole_kilobyte);
    TEST("sysex_pattern_dump_written_and_acked",
         test_pattern_dump_is_written_and_acknowledged);
    TEST("sysex_corrupt_dump_rejected_and_eeprom_unchanged",
         test_corrupt_dump_is_rejected_and_changes_nothing);
    TEST("sysex_out_of_range_track_refused",
         test_out_of_range_track_is_refused);
    TEST("sysex_ignored_off_the_sysex_page",
         test_requests_are_ignored_off_the_sysex_page);

    const char *elf = getenv("NAVA_ELF");
    return test_run_all(elf ? elf : NAVA_ELF_PATH);
}
