#ifndef DPP_CODEX_LIGHTING_H
#define DPP_CODEX_LIGHTING_H

#include <stddef.h>
#include <stdint.h>

#define CODEX_LIGHT_THREAD_COUNT 6
#define CODEX_LIGHT_LED_COUNT 20

typedef enum {
    CODEX_EFFECT_OFF = 0,
    CODEX_EFFECT_SOLID = 1,
    CODEX_EFFECT_SNAKE = 2,
    CODEX_EFFECT_RAINBOW = 3,
    CODEX_EFFECT_BREATH = 4,
    CODEX_EFFECT_GRADIENT = 5,
    CODEX_EFFECT_SHALLOW_BREATH = 6,
} codex_effect_t;

typedef struct {
    uint32_t color;
    uint8_t brightness;
    uint8_t effect;
    uint8_t speed;
} codex_light_config_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} codex_rgb_t;

typedef struct {
    codex_light_config_t ambient;
    codex_light_config_t thread[CODEX_LIGHT_THREAD_COUNT];
    uint8_t master_brightness_percent;
    uint8_t animation_speed_percent;
    int8_t selected_slot;
    uint8_t output_enabled;
} codex_lighting_t;

void codex_lighting_init(codex_lighting_t *model);
void codex_lighting_set_ambient(codex_lighting_t *model,
                                codex_light_config_t config);
int codex_lighting_set_thread(codex_lighting_t *model, size_t slot,
                              codex_light_config_t config);
void codex_lighting_set_master_brightness(codex_lighting_t *model,
                                          uint8_t percent);
void codex_lighting_set_animation_speed(codex_lighting_t *model,
                                        uint8_t percent);
void codex_lighting_set_selected(codex_lighting_t *model, int slot);
void codex_lighting_set_output_enabled(codex_lighting_t *model,
                                       uint8_t enabled);
void codex_lighting_render(const codex_lighting_t *model, uint32_t tick,
                           codex_rgb_t output[CODEX_LIGHT_LED_COUNT]);

#endif
