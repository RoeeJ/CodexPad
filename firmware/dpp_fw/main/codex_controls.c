#include "codex_controls.h"

#include <stdio.h>
#include <string.h>

#define CODEX_EVENT_RELEASE 0
#define CODEX_EVENT_PRESS 1

int codex_controls_translate(uint8_t switch_id, uint8_t event_type,
                             codex_control_event_t *output)
{
    static const char *const action_keys[] = {
        /*
         * Physical keys 6..15:
         *   R2B3 R2B4 R3B1 R3B2 R3B3 R3B4 R4B1 R4B2 R4B3 R4B4
         * Codex's ACT10/ACT11 inputs are one combined microphone slot, so one
         * duckyPad key uses ACT10. ACT12 occupies R3B4, and row four mirrors
         * the four preceding non-microphone action placements.
         */
        "ACT06", "ACT07", "ACT08", "ACT09", "ACT10",
        "ACT12", "ACT06", "ACT07", "ACT08", "ACT09",
    };
    if (output == NULL ||
        (event_type != CODEX_EVENT_RELEASE && event_type != CODEX_EVENT_PRESS))
        return 0;
    memset(output, 0, sizeof(*output));
    output->agent = -1;
    if (switch_id < 6) {
        snprintf(output->key, sizeof(output->key), "AG%02u", switch_id);
        output->agent = (int8_t)switch_id;
    } else if (switch_id >= 6 && switch_id <= 15) {
        snprintf(output->key, sizeof(output->key), "%s",
                 action_keys[switch_id - 6]);
    } else if (switch_id == 22) {
        snprintf(output->key, sizeof(output->key), "ENC_TO");
    } else {
        return 0;
    }
    output->action = event_type == CODEX_EVENT_PRESS ? 1 : 0;
    return 1;
}

codex_local_control_t codex_controls_local(uint8_t switch_id)
{
    switch (switch_id) {
    case 16:
        return CODEX_LOCAL_STATUS;
    case 17:
        return CODEX_LOCAL_DIMMER;
    case 18:
        return CODEX_LOCAL_BRIGHTER;
    case 19:
        return CODEX_LOCAL_SETTINGS;
    default:
        return CODEX_LOCAL_NONE;
    }
}

int codex_controls_translate_encoder(uint8_t clockwise,
                                     codex_control_event_t *output)
{
    if (output == NULL)
        return 0;
    memset(output, 0, sizeof(*output));
    snprintf(output->key, sizeof(output->key), "%s",
             clockwise ? "ENC_CW" : "ENC_CC");
    output->action = 2;
    output->agent = -1;
    return 1;
}
