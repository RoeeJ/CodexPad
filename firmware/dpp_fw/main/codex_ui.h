#ifndef DPP_CODEX_UI_H
#define DPP_CODEX_UI_H

#include <stdint.h>

#include "codex_lighting.h"
#include "codex_settings.h"

#define CODEX_UI_SETTINGS_COUNT 9u
#define CODEX_UI_DIAGNOSTICS_PAGES 7u

typedef enum {
    CODEX_UI_HOME = 0,
    CODEX_UI_OVERVIEW,
    CODEX_UI_SETTINGS,
    CODEX_UI_DIAGNOSTICS,
    CODEX_UI_ABOUT,
} codex_ui_page_t;

typedef struct {
    uint32_t uptime_ms;
    uint32_t rx_dropped;
    uint32_t rx_rejected;
    uint32_t rx_timeouts;
    uint32_t tx_failed;
    uint32_t rpc_requests;
    uint32_t input_dropped;
    uint32_t current_free_memory;
    uint32_t minimum_free_memory;
    uint8_t usb_connected;
    uint8_t rpc_active;
    uint8_t selected_agent;
    uint8_t boot_codex;
    uint8_t last_input_id;
    uint8_t last_input_type;
    uint8_t led_test_active;
    const char *last_error;
} codex_ui_diagnostics_t;

void codex_ui_set_display(uint8_t enabled, uint8_t contrast);
void codex_ui_render_home(uint8_t usb_connected, unsigned selected_agent,
                          const codex_light_config_t *selected_light,
                          const char *context);
void codex_ui_render_overview(const codex_lighting_t *lighting,
                              unsigned selected_agent);
void codex_ui_render_settings(const codex_settings_t *settings,
                              unsigned selected_item);
void codex_ui_render_diagnostics(const codex_ui_diagnostics_t *diagnostics,
                                 unsigned page);
void codex_ui_render_about(void);
const char *codex_ui_effect_name(uint8_t effect);
const char *codex_ui_color_name(uint32_t color);

#endif
