#include "codex_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "codex_rpc.h"

#define CODEX_UI_LINE_SIZE 24

static void begin_screen(const char *title)
{
    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString((char *)title, Font_7x10, White);
    ssd1306_Line(0, 12, SSD1306_WIDTH - 1, 12, White);
}

static void line_at(unsigned row, const char *format, ...)
{
    char line[CODEX_UI_LINE_SIZE];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    ssd1306_SetCursor(0, (uint8_t)(16u + row * 9u));
    ssd1306_WriteString(line, Font_6x8, White);
}

static void finish_screen(void)
{
    ssd1306_UpdateScreen();
}

void codex_ui_set_display(uint8_t enabled, uint8_t contrast)
{
    if (enabled) {
        ssd1306_SetContrast(contrast);
        ssd1306_SetDisplayOn(1);
    } else {
        ssd1306_SetDisplayOn(0);
    }
}

const char *codex_ui_effect_name(uint8_t effect)
{
    static const char *const names[] = {
        "off", "solid", "snake", "rainbow", "breath", "gradient", "shallow",
    };
    return effect <= CODEX_EFFECT_SHALLOW_BREATH ? names[effect] : "unknown";
}

const char *codex_ui_color_name(uint32_t color)
{
    uint8_t red = (uint8_t)(color >> 16);
    uint8_t green = (uint8_t)(color >> 8);
    uint8_t blue = (uint8_t)color;
    uint8_t high = red;
    uint8_t low = red;
    if (green > high)
        high = green;
    if (blue > high)
        high = blue;
    if (green < low)
        low = green;
    if (blue < low)
        low = blue;
    if (high < 20)
        return "black";
    if ((unsigned)high - low < 32)
        return high > 180 ? "white" : "gray";
    if (red > 160 && green > 100 && blue < 80)
        return "yellow";
    if (red > 120 && blue > 100 && green < 100)
        return "magenta";
    if (green > 100 && blue > 100 && red < 100)
        return "cyan";
    if (red >= green && red >= blue)
        return "red";
    if (green >= red && green >= blue)
        return "green";
    return "blue";
}

void codex_ui_render_home(uint8_t usb_connected, unsigned selected_agent,
                          const codex_light_config_t *selected_light,
                          const char *context)
{
    begin_screen("CODEX");
    line_at(0, "USB: %s", usb_connected ? "connected" : "waiting");
    line_at(1, "Selected: Agent %u", selected_agent + 1u);
    if (selected_light != NULL) {
        line_at(2, "State: %s", codex_ui_color_name(selected_light->color));
        line_at(3, "Effect: %s",
                codex_ui_effect_name(selected_light->effect));
    } else {
        line_at(2, "State: unavailable");
    }
    line_at(5, "%s", context != NULL ? context : "Ready");
    line_at(8, "Hold upper: settings");
    line_at(9, "Hold lower: overview");
    finish_screen();
}

void codex_ui_render_overview(const codex_lighting_t *lighting,
                              unsigned selected_agent)
{
    unsigned index;
    begin_screen("AGENT OVERVIEW");
    for (index = 0; index < CODEX_LIGHT_THREAD_COUNT; ++index) {
        const codex_light_config_t *config =
            lighting != NULL ? &lighting->thread[index] : NULL;
        line_at(index, "%c A%u %-7s %-7s",
                index == selected_agent ? '>' : ' ', index + 1u,
                config != NULL ? codex_ui_color_name(config->color) : "none",
                config != NULL ? codex_ui_effect_name(config->effect) : "");
    }
    line_at(8, "Turn lower to select");
    line_at(9, "Press lower to focus");
    finish_screen();
}

static void setting_value(char output[CODEX_UI_LINE_SIZE],
                          const codex_settings_t *settings, unsigned item)
{
    switch (item) {
    case 0:
        snprintf(output, CODEX_UI_LINE_SIZE, "LED brightness %u%%",
                 settings->led_brightness_percent);
        break;
    case 1:
        snprintf(output, CODEX_UI_LINE_SIZE, "OLED contrast %u",
                 settings->oled_contrast);
        break;
    case 2:
        snprintf(output, CODEX_UI_LINE_SIZE, "OLED timeout %us",
                 settings->oled_timeout_seconds);
        break;
    case 3:
        snprintf(output, CODEX_UI_LINE_SIZE, "Auto dim %s",
                 settings->auto_dim ? "on" : "off");
        break;
    case 4:
        snprintf(output, CODEX_UI_LINE_SIZE, "Boot mode %s",
                 settings->boot_codex ? "Codex" : "stock");
        break;
    case 5:
        snprintf(output, CODEX_UI_LINE_SIZE, "Animation %u%%",
                 settings->animation_speed_percent);
        break;
    case 6:
        snprintf(output, CODEX_UI_LINE_SIZE, "LED idle off %us",
                 settings->lighting_timeout_seconds);
        break;
    case 7:
        snprintf(output, CODEX_UI_LINE_SIZE, "Restore defaults");
        break;
    case 8:
    default:
        snprintf(output, CODEX_UI_LINE_SIZE, "Firmware / about");
        break;
    }
}

void codex_ui_render_settings(const codex_settings_t *settings,
                              unsigned selected_item)
{
    char value[CODEX_UI_LINE_SIZE];
    unsigned item;
    unsigned first;
    if (settings == NULL)
        return;
    if (selected_item >= CODEX_UI_SETTINGS_COUNT)
        selected_item = 0;
    first = selected_item > 3 ? selected_item - 3 : 0;
    if (first + 6 > CODEX_UI_SETTINGS_COUNT)
        first = CODEX_UI_SETTINGS_COUNT - 6;
    begin_screen("LOCAL SETTINGS");
    for (item = first; item < first + 6; ++item) {
        setting_value(value, settings, item);
        line_at(item - first, "%c%s", item == selected_item ? '>' : ' ',
                value);
    }
    line_at(8, "Lower: choose");
    line_at(9, "Upper/+/-: change");
    line_at(10, "Press lower: close");
    finish_screen();
}

void codex_ui_render_diagnostics(const codex_ui_diagnostics_t *diagnostics,
                                 unsigned page)
{
    uint32_t seconds;
    if (diagnostics == NULL)
        return;
    page %= CODEX_UI_DIAGNOSTICS_PAGES;
    begin_screen("DIAGNOSTICS");
    line_at(0, "Page %u/%u", page + 1u, CODEX_UI_DIAGNOSTICS_PAGES);
    switch (page) {
    case 0:
        line_at(2, "USB: %s",
                diagnostics->usb_connected ? "connected" : "waiting");
        line_at(3, "RPC: %s (%lu)",
                diagnostics->rpc_active ? "active" : "waiting",
                (unsigned long)diagnostics->rpc_requests);
        line_at(4, "ID: 303a:8297");
        line_at(5, "FW/proto: %s", CODEX_PROTOCOL_VERSION);
        line_at(6, "Mode: %s",
                diagnostics->boot_codex ? "Codex next boot" : "stock next boot");
        break;
    case 1:
        line_at(2, "RX drop: %lu", (unsigned long)diagnostics->rx_dropped);
        line_at(3, "RX reject: %lu",
                (unsigned long)diagnostics->rx_rejected);
        line_at(4, "RX timeout: %lu",
                (unsigned long)diagnostics->rx_timeouts);
        line_at(5, "TX failed: %lu",
                (unsigned long)diagnostics->tx_failed);
        line_at(6, "Input drop: %lu",
                (unsigned long)diagnostics->input_dropped);
        break;
    case 2:
        seconds = diagnostics->uptime_ms / 1000u;
        line_at(2, "Uptime: %lus", (unsigned long)seconds);
        line_at(3, "Heap: %lu", (unsigned long)diagnostics->current_free_memory);
        line_at(4, "Min heap: %lu",
                (unsigned long)diagnostics->minimum_free_memory);
        line_at(5, "Selected: Agent %u",
                diagnostics->selected_agent + 1u);
        break;
    case 3:
        line_at(2, "Last protocol error:");
        line_at(4, "%.21s",
                diagnostics->last_error != NULL &&
                        diagnostics->last_error[0] != '\0' ?
                    diagnostics->last_error : "none");
        break;
    case 4:
        line_at(2, "Recovery:");
        line_at(3, "Hold + at boot: stock");
        line_at(4, "Hold - at boot: Codex");
        line_at(5, "Hold both: stock once");
        line_at(7, "Recovery image retained");
        break;
    case 5:
        line_at(2, "LED test: %s",
                diagnostics->led_test_active ? "RUNNING" : "stopped");
        line_at(4, "Press upper to toggle");
        line_at(6, "Host lighting resumes");
        line_at(7, "when test is stopped");
        break;
    case 6:
    default:
        line_at(2, "Input test (live)");
        line_at(4, "Last ID: %u", diagnostics->last_input_id);
        line_at(5, "Last event: %u", diagnostics->last_input_type);
        line_at(7, "Press/turn any control");
        break;
    }
    line_at(10, "Turn lower: next page");
    finish_screen();
}

void codex_ui_render_about(void)
{
    begin_screen("ABOUT");
    line_at(1, "duckyPad Pro");
    line_at(2, "Codex Controller");
    line_at(3, "Firmware %s", CODEX_PROTOCOL_VERSION);
    line_at(4, "Protocol %s", CODEX_PROTOCOL_VERSION);
    line_at(6, "Clean-room wired build");
    line_at(8, "Bluetooth: unavailable");
    line_at(9, "in wired release");
    finish_screen();
}
