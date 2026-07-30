#ifndef DPP_CODEX_MODE_H
#define DPP_CODEX_MODE_H

#include <stddef.h>
#include <stdint.h>

void codex_mode_run(void);
void codex_mode_receive_hid(const uint8_t *payload, size_t payload_length);
void codex_mode_set_active(uint8_t active);
uint8_t codex_mode_is_active(void);

#endif
