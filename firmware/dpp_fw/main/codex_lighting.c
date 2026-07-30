#include "codex_lighting.h"

#include <string.h>

static uint8_t scale8(uint8_t value, uint8_t scale)
{
    return (uint8_t)(((uint16_t)value * scale + 127u) / 255u);
}

static codex_rgb_t wheel(uint8_t position)
{
    codex_rgb_t result;
    if (position < 85) {
        result.red = (uint8_t)(255 - position * 3);
        result.green = (uint8_t)(position * 3);
        result.blue = 0;
    } else if (position < 170) {
        position = (uint8_t)(position - 85);
        result.red = 0;
        result.green = (uint8_t)(255 - position * 3);
        result.blue = (uint8_t)(position * 3);
    } else {
        position = (uint8_t)(position - 170);
        result.red = (uint8_t)(position * 3);
        result.green = 0;
        result.blue = (uint8_t)(255 - position * 3);
    }
    return result;
}

static uint8_t triangle(uint32_t tick)
{
    uint8_t phase = (uint8_t)tick;
    return phase < 128 ? (uint8_t)(phase * 2) :
                         (uint8_t)((255 - phase) * 2);
}

static codex_rgb_t render_one(codex_light_config_t config, size_t index,
                              size_t count, uint32_t tick)
{
    tick = config.speed == 0 ? 0 :
           (uint32_t)(((uint64_t)tick * (32u + config.speed)) / 128u);
    codex_rgb_t result = {
        .red = (uint8_t)(config.color >> 16),
        .green = (uint8_t)(config.color >> 8),
        .blue = (uint8_t)config.color,
    };
    uint8_t intensity = config.brightness;

    switch (config.effect) {
    case CODEX_EFFECT_OFF:
        intensity = 0;
        break;
    case CODEX_EFFECT_SNAKE: {
        size_t head = count == 0 ? 0 : (tick / 3u) % count;
        intensity = index == head ? intensity : scale8(intensity, 24);
        break;
    }
    case CODEX_EFFECT_RAINBOW:
        result = wheel((uint8_t)(tick * 3u +
                                 (count == 0 ? 0 : index * 255u / count)));
        break;
    case CODEX_EFFECT_BREATH:
        intensity = scale8(intensity, triangle(tick * 3u));
        break;
    case CODEX_EFFECT_GRADIENT: {
        codex_rgb_t other =
            wheel((uint8_t)(tick + (count == 0 ? 0 : index * 255u / count)));
        result.red = (uint8_t)(((uint16_t)result.red + other.red) / 2u);
        result.green = (uint8_t)(((uint16_t)result.green + other.green) / 2u);
        result.blue = (uint8_t)(((uint16_t)result.blue + other.blue) / 2u);
        break;
    }
    case CODEX_EFFECT_SHALLOW_BREATH:
        intensity =
            scale8(intensity, (uint8_t)(178u + triangle(tick * 3u) * 77u / 255u));
        break;
    case CODEX_EFFECT_SOLID:
    default:
        break;
    }
    result.red = scale8(result.red, intensity);
    result.green = scale8(result.green, intensity);
    result.blue = scale8(result.blue, intensity);
    return result;
}

void codex_lighting_init(codex_lighting_t *model)
{
    if (model == NULL)
        return;
    memset(model, 0, sizeof(*model));
    model->ambient.effect = CODEX_EFFECT_OFF;
    model->master_brightness_percent = 100;
    model->animation_speed_percent = 100;
    model->selected_slot = -1;
    model->output_enabled = 1;
}

void codex_lighting_set_ambient(codex_lighting_t *model,
                                codex_light_config_t config)
{
    if (model != NULL)
        model->ambient = config;
}

int codex_lighting_set_thread(codex_lighting_t *model, size_t slot,
                              codex_light_config_t config)
{
    if (model == NULL || slot >= CODEX_LIGHT_THREAD_COUNT)
        return 0;
    model->thread[slot] = config;
    return 1;
}

void codex_lighting_set_master_brightness(codex_lighting_t *model,
                                          uint8_t percent)
{
    if (model != NULL)
        model->master_brightness_percent = percent > 100 ? 100 : percent;
}

void codex_lighting_set_animation_speed(codex_lighting_t *model,
                                        uint8_t percent)
{
    if (model != NULL)
        model->animation_speed_percent = percent > 100 ? 100 : percent;
}

void codex_lighting_set_selected(codex_lighting_t *model, int slot)
{
    if (model == NULL)
        return;
    model->selected_slot =
        slot >= 0 && slot < CODEX_LIGHT_THREAD_COUNT ? (int8_t)slot : -1;
}

void codex_lighting_set_output_enabled(codex_lighting_t *model,
                                       uint8_t enabled)
{
    if (model != NULL)
        model->output_enabled = enabled ? 1 : 0;
}

void codex_lighting_render(const codex_lighting_t *model, uint32_t tick,
                           codex_rgb_t output[CODEX_LIGHT_LED_COUNT])
{
    size_t index;
    uint8_t master;
    if (model == NULL || output == NULL)
        return;
    if (!model->output_enabled) {
        memset(output, 0, sizeof(*output) * CODEX_LIGHT_LED_COUNT);
        return;
    }
    tick = (uint32_t)(((uint64_t)tick * model->animation_speed_percent) / 100u);
    for (index = 0; index < CODEX_LIGHT_THREAD_COUNT; ++index)
        output[index] = render_one(model->thread[index], index,
                                   CODEX_LIGHT_THREAD_COUNT, tick);
    for (; index < CODEX_LIGHT_LED_COUNT; ++index)
        output[index] =
            render_one(model->ambient, index - CODEX_LIGHT_THREAD_COUNT,
                       CODEX_LIGHT_LED_COUNT - CODEX_LIGHT_THREAD_COUNT, tick);
    if (model->selected_slot >= 0) {
        codex_rgb_t *selected = &output[(unsigned)model->selected_slot];
        uint8_t highlight =
            (uint8_t)(28u + triangle(tick * 2u) * 36u / 255u);
        selected->red = (uint8_t)(selected->red +
            ((uint16_t)(255u - selected->red) * highlight) / 255u);
        selected->green = (uint8_t)(selected->green +
            ((uint16_t)(255u - selected->green) * highlight) / 255u);
        selected->blue = (uint8_t)(selected->blue +
            ((uint16_t)(255u - selected->blue) * highlight) / 255u);
    }
    master = (uint8_t)((model->master_brightness_percent * 255u + 50u) / 100u);
    for (index = 0; index < CODEX_LIGHT_LED_COUNT; ++index) {
        output[index].red = scale8(output[index].red, master);
        output[index].green = scale8(output[index].green, master);
        output[index].blue = scale8(output[index].blue, master);
    }
}
