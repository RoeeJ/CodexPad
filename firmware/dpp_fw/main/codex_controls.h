#ifndef DPP_CODEX_CONTROLS_H
#define DPP_CODEX_CONTROLS_H

#include <stdint.h>

typedef struct {
    char key[8];
    uint8_t action;
    int8_t agent;
} codex_control_event_t;

typedef enum {
    CODEX_LOCAL_NONE = 0,
    CODEX_LOCAL_STATUS,
    CODEX_LOCAL_DIMMER,
    CODEX_LOCAL_BRIGHTER,
    CODEX_LOCAL_SETTINGS,
} codex_local_control_t;

/* Returns one only for a supported press/release edge. */
int codex_controls_translate(uint8_t switch_id, uint8_t event_type,
                             codex_control_event_t *output);
int codex_controls_translate_encoder(uint8_t clockwise,
                                     codex_control_event_t *output);
codex_local_control_t codex_controls_local(uint8_t switch_id);

#endif
