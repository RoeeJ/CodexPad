#ifndef DPP_CODEX_SETTINGS_H
#define DPP_CODEX_SETTINGS_H

#include <stdint.h>

#define CODEX_SETTINGS_VERSION 1u
#define CODEX_LED_BRIGHTNESS_STEP 10u
#define CODEX_OLED_CONTRAST_STEP 16u
#define CODEX_OLED_TIMEOUT_STEP_SECONDS 15u
#define CODEX_ANIMATION_SPEED_STEP 10u

typedef struct {
    uint8_t version;
    uint8_t led_brightness_percent;
    uint8_t oled_contrast;
    uint8_t auto_dim;
    uint8_t animation_speed_percent;
    uint8_t boot_codex;
    uint16_t oled_timeout_seconds;
    uint16_t lighting_timeout_seconds;
} codex_settings_t;

void codex_settings_defaults(codex_settings_t *settings);
void codex_settings_sanitize(codex_settings_t *settings);

/*
 * Storage is isolated to the "codexpad" NVS namespace. Load failures leave
 * defaults in place and never erase NVS, preserving stock Bluetooth state.
 */
int codex_settings_load(codex_settings_t *settings);
int codex_settings_save(const codex_settings_t *settings);

#endif
