#ifndef DPP_CODEX_HID_H
#define DPP_CODEX_HID_H

#include <stddef.h>
#include <stdint.h>

/*
 * Transmit one logical RPC message over the Codex vendor-HID interface.
 * JSON messages are LF terminated for the host parser. The module serializes
 * complete logical messages so responses and control events cannot interleave.
 */
int codex_hid_init(void);
int codex_hid_send_bytes(const uint8_t *bytes, size_t length);
int codex_hid_send_json(const char *json);

#endif
