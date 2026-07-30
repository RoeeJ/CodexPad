#ifndef DPP_CODEX_WIRE_H
#define DPP_CODEX_WIRE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CODEX_HID_REPORT_ID 6u
#define CODEX_HID_REPORT_SIZE 64u
#define CODEX_HID_PAYLOAD_SIZE 61u
#define CODEX_HID_CHANNEL_DEBUG 1u
#define CODEX_HID_CHANNEL_RPC 2u

typedef enum {
    CODEX_WIRE_OK = 0,
    CODEX_WIRE_MESSAGE_COMPLETE = 1,
    CODEX_WIRE_INVALID_ARGUMENT = -1,
    CODEX_WIRE_INVALID_REPORT = -2,
    CODEX_WIRE_INVALID_CHANNEL = -3,
    CODEX_WIRE_OVERFLOW = -4,
} codex_wire_result_t;

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t length;
} codex_reassembler_t;

void codex_reassembler_init(
    codex_reassembler_t *reassembler,
    uint8_t *buffer,
    size_t capacity
);

void codex_reassembler_reset(codex_reassembler_t *reassembler);

codex_wire_result_t codex_wire_encode_report(
    uint8_t channel,
    const uint8_t *payload,
    size_t payload_length,
    uint8_t report[CODEX_HID_REPORT_SIZE]
);

codex_wire_result_t codex_wire_decode_report(
    const uint8_t report[CODEX_HID_REPORT_SIZE],
    uint8_t *channel,
    const uint8_t **payload,
    size_t *payload_length
);

/*
 * Feed one report into a bounded RPC-channel reassembler.
 *
 * On CODEX_WIRE_MESSAGE_COMPLETE, message points into the caller-owned
 * reassembly buffer and message_length excludes CR/LF. Host requests complete
 * when a balanced top-level JSON object closes; newline-terminated traffic is
 * also accepted. The caller must consume the message before feeding another
 * report.
 */
codex_wire_result_t codex_reassembler_feed(
    codex_reassembler_t *reassembler,
    const uint8_t report[CODEX_HID_REPORT_SIZE],
    const uint8_t **message,
    size_t *message_length
);

#ifdef __cplusplus
}
#endif

#endif
