#include "codex_settings.h"

#include <stddef.h>
#include <string.h>

#ifndef CODEX_SETTINGS_NO_NVS
#include "nvs.h"
#include "nvs_flash.h"
#endif

#define CODEX_NVS_NAMESPACE "codexpad"
#define CODEX_NVS_SETTINGS_KEY "settings_v1"

static uint8_t clamp_u8(uint8_t value, uint8_t maximum)
{
    return value > maximum ? maximum : value;
}

void codex_settings_defaults(codex_settings_t *settings)
{
    if (settings == NULL)
        return;
    *settings = (codex_settings_t){
        .version = CODEX_SETTINGS_VERSION,
        .led_brightness_percent = 100,
        .oled_contrast = 255,
        .auto_dim = 1,
        .animation_speed_percent = 100,
        .boot_codex = 1,
        .oled_timeout_seconds = 60,
        .lighting_timeout_seconds = 300,
    };
}

void codex_settings_sanitize(codex_settings_t *settings)
{
    if (settings == NULL)
        return;
    settings->version = CODEX_SETTINGS_VERSION;
    settings->led_brightness_percent =
        clamp_u8(settings->led_brightness_percent, 100);
    settings->auto_dim = settings->auto_dim ? 1 : 0;
    settings->animation_speed_percent =
        clamp_u8(settings->animation_speed_percent, 100);
    settings->boot_codex = settings->boot_codex ? 1 : 0;
    if (settings->oled_timeout_seconds > 600)
        settings->oled_timeout_seconds = 600;
    if (settings->lighting_timeout_seconds > 3600)
        settings->lighting_timeout_seconds = 3600;
}

int codex_settings_load(codex_settings_t *settings)
{
#ifdef CODEX_SETTINGS_NO_NVS
    if (settings == NULL)
        return 0;
    codex_settings_defaults(settings);
    return 0;
#else
    nvs_handle_t handle;
    size_t size = sizeof(*settings);
    esp_err_t status;

    if (settings == NULL)
        return 0;
    codex_settings_defaults(settings);
    status = nvs_flash_init();
    if (status != ESP_OK)
        return 0;
    status = nvs_open(CODEX_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (status != ESP_OK)
        return 0;
    codex_settings_t stored;
    memset(&stored, 0, sizeof(stored));
    status = nvs_get_blob(handle, CODEX_NVS_SETTINGS_KEY, &stored, &size);
    if (status != ESP_OK) {
        uint8_t legacy_mode;
        if (nvs_get_u8(handle, "codex_mode", &legacy_mode) == ESP_OK)
            settings->boot_codex = legacy_mode ? 1 : 0;
    }
    nvs_close(handle);
    if (status != ESP_OK || size != sizeof(stored) ||
        stored.version != CODEX_SETTINGS_VERSION)
        return 0;
    codex_settings_sanitize(&stored);
    *settings = stored;
    return 1;
#endif
}

int codex_settings_save(const codex_settings_t *settings)
{
#ifdef CODEX_SETTINGS_NO_NVS
    return settings != NULL;
#else
    nvs_handle_t handle;
    esp_err_t status;
    codex_settings_t stored;

    if (settings == NULL)
        return 0;
    stored = *settings;
    codex_settings_sanitize(&stored);
    status = nvs_flash_init();
    if (status != ESP_OK)
        return 0;
    status = nvs_open(CODEX_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (status != ESP_OK)
        return 0;
    status = nvs_set_blob(handle, CODEX_NVS_SETTINGS_KEY, &stored,
                          sizeof(stored));
    if (status == ESP_OK)
        status = nvs_set_u8(handle, "codex_mode", stored.boot_codex);
    if (status == ESP_OK)
        status = nvs_commit(handle);
    nvs_close(handle);
    return status == ESP_OK;
#endif
}
