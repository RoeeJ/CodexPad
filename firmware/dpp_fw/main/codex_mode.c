#include "codex_mode.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tusb.h"

#include "codex_controls.h"
#include "codex_hid.h"
#include "codex_lighting.h"
#include "codex_rpc.h"
#include "codex_settings.h"
#include "codex_ui.h"
#include "codex_wire.h"
#include "hid_task.h"
#include "input_task.h"
#include "neopixel_task.h"

#define CODEX_RX_QUEUE_DEPTH 8
#define CODEX_TASK_STACK_SIZE 7168
#define CODEX_REASSEMBLY_TIMEOUT_MS 1000
#define CODEX_LIGHTING_FRAME_MS 40
#define CODEX_SETTINGS_SAVE_DELAY_MS 1000
#define CODEX_UI_REFRESH_MS 1000
#define CODEX_CONTEXT_SIZE 24

typedef struct {
    uint8_t report[CODEX_HID_REPORT_SIZE];
} codex_rx_item_t;

static const char *TAG = "CODEX";
static QueueHandle_t codex_rx_queue;
static SemaphoreHandle_t codex_state_mutex;
static SemaphoreHandle_t codex_oled_mutex;
static codex_lighting_t codex_lighting;
static codex_settings_t codex_settings;
static codex_ui_page_t codex_ui_page = CODEX_UI_HOME;
static unsigned codex_settings_item;
static unsigned codex_diagnostics_page;
static unsigned codex_selected_agent;
static uint8_t codex_screen_state;
static uint8_t codex_led_test_active;
static uint8_t codex_last_input_id;
static uint8_t codex_last_input_type;
static uint8_t codex_settings_dirty;
static uint8_t codex_active = 1;
static uint32_t codex_long_press_mask;
static TickType_t codex_last_input_tick;
static TickType_t codex_settings_changed_tick;
static char codex_context[CODEX_CONTEXT_SIZE] = "Ready";
static char codex_last_error[CODEX_RPC_LAST_ERROR_SIZE];
static volatile uint32_t codex_rx_dropped;
static volatile uint32_t codex_rx_rejected;
static volatile uint32_t codex_rx_timeouts;
static volatile uint32_t codex_tx_failed;
static volatile uint32_t codex_rpc_requests;

static uint32_t ticks_to_ms(TickType_t ticks)
{
    return (uint32_t)ticks * (uint32_t)portTICK_PERIOD_MS;
}

static uint32_t codex_uptime_ms(void)
{
    return ticks_to_ms(xTaskGetTickCount());
}

static void codex_set_error(const char *message)
{
    if (codex_state_mutex == NULL || message == NULL)
        return;
    if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        snprintf(codex_last_error, sizeof(codex_last_error), "%s", message);
        xSemaphoreGive(codex_state_mutex);
    }
}

static void codex_set_context(const char *message)
{
    if (codex_state_mutex == NULL || message == NULL)
        return;
    if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        snprintf(codex_context, sizeof(codex_context), "%s", message);
        xSemaphoreGive(codex_state_mutex);
    }
}

static void codex_collect_ui_diagnostics(
    codex_ui_diagnostics_t *diagnostics, char error[CODEX_RPC_LAST_ERROR_SIZE])
{
    memset(diagnostics, 0, sizeof(*diagnostics));
    diagnostics->uptime_ms = codex_uptime_ms();
    diagnostics->rx_dropped = codex_rx_dropped;
    diagnostics->rx_rejected = codex_rx_rejected;
    diagnostics->rx_timeouts = codex_rx_timeouts;
    diagnostics->tx_failed = codex_tx_failed;
    diagnostics->rpc_requests = codex_rpc_requests;
    diagnostics->input_dropped = input_get_dropped_event_count();
    diagnostics->current_free_memory = esp_get_free_heap_size();
    diagnostics->minimum_free_memory = esp_get_minimum_free_heap_size();
    diagnostics->usb_connected = tud_mounted() ? 1 : 0;
    diagnostics->rpc_active = codex_rpc_requests != 0;
    diagnostics->selected_agent = (uint8_t)codex_selected_agent;
    diagnostics->boot_codex = codex_settings.boot_codex;
    diagnostics->last_input_id = codex_last_input_id;
    diagnostics->last_input_type = codex_last_input_type;
    diagnostics->led_test_active = codex_led_test_active;
    snprintf(error, CODEX_RPC_LAST_ERROR_SIZE, "%s", codex_last_error);
    diagnostics->last_error = error;
}

static void codex_render_current(void)
{
    codex_settings_t settings;
    codex_lighting_t lighting;
    codex_ui_page_t page;
    codex_ui_diagnostics_t diagnostics;
    char context[CODEX_CONTEXT_SIZE];
    char error[CODEX_RPC_LAST_ERROR_SIZE];
    unsigned selected;
    unsigned setting_item;
    unsigned diagnostics_page;
    uint8_t screen_state;

    if (codex_state_mutex == NULL || codex_oled_mutex == NULL)
        return;
    if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(30)) != pdTRUE)
        return;
    settings = codex_settings;
    lighting = codex_lighting;
    page = codex_ui_page;
    selected = codex_selected_agent;
    setting_item = codex_settings_item;
    diagnostics_page = codex_diagnostics_page;
    screen_state = codex_screen_state;
    snprintf(context, sizeof(context), "%s", codex_context);
    codex_collect_ui_diagnostics(&diagnostics, error);
    xSemaphoreGive(codex_state_mutex);

    if (xSemaphoreTake(codex_oled_mutex, pdMS_TO_TICKS(50)) != pdTRUE)
        return;
    if (screen_state == 2) {
        codex_ui_set_display(0, settings.oled_contrast);
        xSemaphoreGive(codex_oled_mutex);
        return;
    }
    uint8_t contrast = settings.oled_contrast;
    if (screen_state == 1) {
        contrast = (uint8_t)(contrast / 3u);
        if (settings.oled_contrast != 0 && contrast < 8)
            contrast = 8;
    }
    codex_ui_set_display(1, contrast);
    switch (page) {
    case CODEX_UI_OVERVIEW:
        codex_ui_render_overview(&lighting, selected);
        break;
    case CODEX_UI_SETTINGS:
        codex_ui_render_settings(&settings, setting_item);
        break;
    case CODEX_UI_DIAGNOSTICS:
        codex_ui_render_diagnostics(&diagnostics, diagnostics_page);
        break;
    case CODEX_UI_ABOUT:
        codex_ui_render_about();
        break;
    case CODEX_UI_HOME:
    default:
        codex_ui_render_home(diagnostics.usb_connected, selected,
                             &lighting.thread[selected], context);
        break;
    }
    xSemaphoreGive(codex_oled_mutex);
}

void codex_mode_set_active(uint8_t active)
{
    codex_active = active ? 1 : 0;
}

uint8_t codex_mode_is_active(void)
{
    return codex_active;
}

static int codex_send_json_text(const char *json)
{
    if (codex_hid_send_json(json))
        return 1;
    ++codex_tx_failed;
    codex_set_error("HID transmit failed");
    ESP_LOGW(TAG, "HID transmit failed");
    return 0;
}

static void codex_rpc_apply_ambient(void *context,
                                    codex_light_config_t config)
{
    (void)context;
    if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        codex_lighting_set_ambient(&codex_lighting, config);
        xSemaphoreGive(codex_state_mutex);
    }
}

static void codex_rpc_apply_thread(void *context, unsigned slot,
                                   codex_light_config_t config)
{
    (void)context;
    if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        codex_lighting_set_thread(&codex_lighting, slot, config);
        xSemaphoreGive(codex_state_mutex);
    }
}

static void codex_rpc_read_diagnostics(
    void *context, codex_rpc_diagnostics_t *diagnostics)
{
    (void)context;
    if (diagnostics == NULL)
        return;
    memset(diagnostics, 0, sizeof(*diagnostics));
    diagnostics->rx_dropped = codex_rx_dropped;
    diagnostics->rx_rejected = codex_rx_rejected;
    diagnostics->rx_timeouts = codex_rx_timeouts;
    diagnostics->tx_failed = codex_tx_failed;
    diagnostics->rpc_requests = codex_rpc_requests;
    diagnostics->input_dropped = input_get_dropped_event_count();
    diagnostics->uptime_ms = codex_uptime_ms();
    diagnostics->current_free_memory = esp_get_free_heap_size();
    diagnostics->minimum_free_memory = esp_get_minimum_free_heap_size();
    diagnostics->usb_connected = tud_mounted() ? 1 : 0;
    if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        diagnostics->selected_agent = (uint8_t)codex_selected_agent;
        diagnostics->last_error = codex_last_error;
        xSemaphoreGive(codex_state_mutex);
    }
}

static const codex_rpc_callbacks_t codex_rpc_callbacks = {
    .context = NULL,
    .apply_ambient = codex_rpc_apply_ambient,
    .apply_thread = codex_rpc_apply_thread,
    /*
     * Codex polls all six slots repeatedly. Their current state is already
     * rendered from codex_lighting; treating each poll as user-facing context
     * makes the last record ("Agent 6") permanently replace useful local
     * action feedback on the OLED.
     */
    .thread_updated = NULL,
    .read_diagnostics = codex_rpc_read_diagnostics,
};

static void codex_rpc_task(void *unused)
{
    static uint8_t storage[CONFIG_DPP_CODEX_MAX_MESSAGE_SIZE];
    codex_reassembler_t reassembler;
    codex_rx_item_t item;
    const uint8_t *message;
    size_t length;
    TickType_t last_frame = 0;
    (void)unused;

    codex_reassembler_init(&reassembler, storage, sizeof(storage));
    for (;;) {
        if (xQueueReceive(codex_rx_queue, &item,
                          pdMS_TO_TICKS(CODEX_REASSEMBLY_TIMEOUT_MS / 4)) !=
            pdTRUE) {
            if (reassembler.length != 0 &&
                xTaskGetTickCount() - last_frame >=
                    pdMS_TO_TICKS(CODEX_REASSEMBLY_TIMEOUT_MS)) {
                ++codex_rx_timeouts;
                codex_set_error("RPC reassembly timeout");
                ESP_LOGW(TAG, "Discarded incomplete timed-out RPC message");
                codex_reassembler_reset(&reassembler);
            }
            continue;
        }
        last_frame = xTaskGetTickCount();
        codex_wire_result_t wire_status = codex_reassembler_feed(
            &reassembler, item.report, &message, &length);
        if (wire_status == CODEX_WIRE_MESSAGE_COMPLETE) {
            codex_rpc_result_t rpc_status;
            ++codex_rpc_requests;
            char *response = codex_rpc_handle(message, length,
                                              &codex_rpc_callbacks,
                                              &rpc_status);
            if (rpc_status != CODEX_RPC_OK)
                codex_set_error(codex_rpc_result_name(rpc_status));
            if (response == NULL || !codex_send_json_text(response))
                codex_set_error("RPC response failed");
            codex_rpc_free(response);
            codex_reassembler_reset(&reassembler);
            codex_render_current();
        } else if (wire_status < 0) {
            char error[CODEX_RPC_LAST_ERROR_SIZE];
            ++codex_rx_rejected;
            snprintf(error, sizeof(error), "Rejected HID frame %d",
                     wire_status);
            codex_set_error(error);
            ESP_LOGW(TAG, "Rejected HID frame: %d", wire_status);
            codex_reassembler_reset(&reassembler);
        }
    }
}

static void codex_notify_control(const codex_control_event_t *control)
{
    char *notification = codex_rpc_build_hid_notification(control);
    if (notification == NULL || !codex_send_json_text(notification))
        codex_set_error("Control notification failed");
    codex_rpc_free(notification);
}

static void codex_note_local_input(uint8_t id, uint8_t type)
{
    if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        codex_last_input_tick = xTaskGetTickCount();
        codex_last_input_id = id;
        codex_last_input_type = type;
        codex_screen_state = 0;
        codex_lighting_set_output_enabled(&codex_lighting, 1);
        xSemaphoreGive(codex_state_mutex);
    }
}

static void codex_set_page(codex_ui_page_t page)
{
    if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        codex_ui_page = page;
        if (page != CODEX_UI_DIAGNOSTICS)
            codex_led_test_active = 0;
        xSemaphoreGive(codex_state_mutex);
    }
    codex_render_current();
}

static void codex_set_selected(unsigned selected)
{
    selected %= CODEX_LIGHT_THREAD_COUNT;
    if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        codex_selected_agent = selected;
        codex_lighting_set_selected(&codex_lighting, (int)selected);
        snprintf(codex_context, sizeof(codex_context), "Selected Agent %u",
                 selected + 1u);
        xSemaphoreGive(codex_state_mutex);
    }
}

static void codex_notify_agent_tap(unsigned selected)
{
    codex_control_event_t control = {
        .action = 1,
        .agent = (int8_t)selected,
    };
    snprintf(control.key, sizeof(control.key), "AG%02u", selected);
    codex_notify_control(&control);
    control.action = 0;
    codex_notify_control(&control);
}

static void codex_select_relative(uint8_t clockwise)
{
    unsigned selected;
    if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return;
    selected = (codex_selected_agent + (clockwise ? 1u : 5u)) % 6u;
    xSemaphoreGive(codex_state_mutex);
    codex_set_selected(selected);
    codex_notify_agent_tap(selected);
    codex_render_current();
}

static int adjusted(int value, int delta, int minimum, int maximum)
{
    value += delta;
    if (value < minimum)
        value = minimum;
    if (value > maximum)
        value = maximum;
    return value;
}

static void codex_mark_settings_changed(void)
{
    codex_settings_sanitize(&codex_settings);
    codex_lighting_set_master_brightness(
        &codex_lighting, codex_settings.led_brightness_percent);
    codex_lighting_set_animation_speed(
        &codex_lighting, codex_settings.animation_speed_percent);
    codex_settings_dirty = 1;
    codex_settings_changed_tick = xTaskGetTickCount();
}

static void codex_adjust_setting(int direction, uint8_t activate)
{
    codex_ui_page_t page_after = CODEX_UI_SETTINGS;
    if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(30)) != pdTRUE)
        return;
    switch (codex_settings_item) {
    case 0:
        codex_settings.led_brightness_percent = (uint8_t)adjusted(
            codex_settings.led_brightness_percent,
            direction * CODEX_LED_BRIGHTNESS_STEP, 0, 100);
        codex_mark_settings_changed();
        break;
    case 1:
        codex_settings.oled_contrast = (uint8_t)adjusted(
            codex_settings.oled_contrast,
            direction * CODEX_OLED_CONTRAST_STEP, 0, 255);
        codex_mark_settings_changed();
        break;
    case 2:
        codex_settings.oled_timeout_seconds = (uint16_t)adjusted(
            codex_settings.oled_timeout_seconds,
            direction * CODEX_OLED_TIMEOUT_STEP_SECONDS, 0, 600);
        codex_mark_settings_changed();
        break;
    case 3:
        if (direction != 0 || activate) {
            codex_settings.auto_dim = !codex_settings.auto_dim;
            codex_mark_settings_changed();
        }
        break;
    case 4:
        if (direction != 0 || activate) {
            codex_settings.boot_codex = !codex_settings.boot_codex;
            codex_mark_settings_changed();
        }
        break;
    case 5:
        codex_settings.animation_speed_percent = (uint8_t)adjusted(
            codex_settings.animation_speed_percent,
            direction * CODEX_ANIMATION_SPEED_STEP, 0, 100);
        codex_mark_settings_changed();
        break;
    case 6:
        codex_settings.lighting_timeout_seconds = (uint16_t)adjusted(
            codex_settings.lighting_timeout_seconds,
            direction * CODEX_OLED_TIMEOUT_STEP_SECONDS, 0, 3600);
        codex_mark_settings_changed();
        break;
    case 7:
        if (activate) {
            codex_settings_defaults(&codex_settings);
            codex_mark_settings_changed();
            snprintf(codex_context, sizeof(codex_context),
                     "Defaults restored");
        }
        break;
    case 8:
        if (activate) {
            codex_ui_page = CODEX_UI_ABOUT;
            page_after = CODEX_UI_ABOUT;
        }
        break;
    default:
        break;
    }
    if (page_after == CODEX_UI_SETTINGS)
        codex_ui_page = CODEX_UI_SETTINGS;
    xSemaphoreGive(codex_state_mutex);
    codex_render_current();
}

static void codex_adjust_led_brightness(int direction)
{
    if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(30)) != pdTRUE)
        return;
    codex_settings.led_brightness_percent = (uint8_t)adjusted(
        codex_settings.led_brightness_percent,
        direction * CODEX_LED_BRIGHTNESS_STEP, 0, 100);
    codex_mark_settings_changed();
    snprintf(codex_context, sizeof(codex_context), "LED brightness %u%%",
             codex_settings.led_brightness_percent);
    codex_ui_page = CODEX_UI_HOME;
    xSemaphoreGive(codex_state_mutex);
    codex_render_current();
}

static void codex_navigate_local(int direction)
{
    if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return;
    if (codex_ui_page == CODEX_UI_SETTINGS) {
        codex_settings_item =
            (codex_settings_item + CODEX_UI_SETTINGS_COUNT +
             (direction > 0 ? 1u : CODEX_UI_SETTINGS_COUNT - 1u)) %
            CODEX_UI_SETTINGS_COUNT;
    } else if (codex_ui_page == CODEX_UI_DIAGNOSTICS) {
        unsigned old_page = codex_diagnostics_page;
        codex_diagnostics_page =
            (codex_diagnostics_page + CODEX_UI_DIAGNOSTICS_PAGES +
             (direction > 0 ? 1u : CODEX_UI_DIAGNOSTICS_PAGES - 1u)) %
            CODEX_UI_DIAGNOSTICS_PAGES;
        if (old_page == 5 && codex_diagnostics_page != 5)
            codex_led_test_active = 0;
    }
    xSemaphoreGive(codex_state_mutex);
    codex_render_current();
}

static void codex_handle_local_key(codex_local_control_t local,
                                   uint8_t event_type)
{
    if (event_type != SW_EVENT_SHORT_PRESS)
        return;
    switch (local) {
    case CODEX_LOCAL_STATUS:
        codex_set_page(CODEX_UI_HOME);
        break;
    case CODEX_LOCAL_DIMMER:
        if (codex_ui_page == CODEX_UI_SETTINGS)
            codex_adjust_setting(-1, 0);
        else
            codex_adjust_led_brightness(-1);
        break;
    case CODEX_LOCAL_BRIGHTER:
        if (codex_ui_page == CODEX_UI_SETTINGS)
            codex_adjust_setting(1, 0);
        else
            codex_adjust_led_brightness(1);
        break;
    case CODEX_LOCAL_SETTINGS:
        if (codex_ui_page == CODEX_UI_SETTINGS)
            codex_set_page(CODEX_UI_DIAGNOSTICS);
        else if (codex_ui_page == CODEX_UI_DIAGNOSTICS)
            codex_set_page(CODEX_UI_HOME);
        else
            codex_set_page(CODEX_UI_SETTINGS);
        break;
    case CODEX_LOCAL_NONE:
    default:
        break;
    }
}

static void codex_handle_encoder_switch(const switch_event_t *event)
{
    codex_control_event_t control;
    /*
     * Execute encoder taps on release. The scanner emits SHORT_PRESS before
     * it knows whether the gesture will become a LONG_PRESS; acting here on
     * press would let a hold activate a setting, and closing a local page on
     * press could then leak a host action when the release arrives.
     * codex_handle_switch() consumes the release after any long press.
     */
    if (event->type == SW_EVENT_SHORT_PRESS)
        return;
    if (event->type != SW_EVENT_RELEASE)
        return;

    if (event->id == RE1_SW) {
        if (codex_ui_page == CODEX_UI_SETTINGS) {
            codex_adjust_setting(0, 1);
            return;
        }
        if (codex_ui_page == CODEX_UI_DIAGNOSTICS) {
            if (codex_diagnostics_page == 5) {
                if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(20)) ==
                    pdTRUE) {
                    codex_led_test_active = !codex_led_test_active;
                    xSemaphoreGive(codex_state_mutex);
                }
                codex_render_current();
            }
            return;
        }
        if (codex_ui_page == CODEX_UI_ABOUT) {
            codex_set_page(CODEX_UI_SETTINGS);
            return;
        }
        memset(&control, 0, sizeof(control));
        snprintf(control.key, sizeof(control.key), "ENC_TO");
        control.agent = -1;
        control.action = 1;
        codex_notify_control(&control);
        control.action = 0;
        codex_notify_control(&control);
        return;
    } else if (event->id == RE2_SW) {
        if (codex_ui_page == CODEX_UI_SETTINGS ||
            codex_ui_page == CODEX_UI_DIAGNOSTICS ||
            codex_ui_page == CODEX_UI_ABOUT) {
            codex_set_page(CODEX_UI_HOME);
            return;
        }
        memset(&control, 0, sizeof(control));
        snprintf(control.key, sizeof(control.key), "AG%02u",
                 codex_selected_agent);
        control.agent = (int8_t)codex_selected_agent;
        control.action = 1;
        codex_notify_control(&control);
        control.action = 0;
        codex_notify_control(&control);
    }
}

static void codex_handle_switch(const switch_event_t *event)
{
    codex_control_event_t control;
    codex_local_control_t local;
    uint32_t mask = event->id < 32 ? 1u << event->id : 0;

    codex_note_local_input(event->id, event->type);
    if (event->type == SW_EVENT_LONG_PRESS) {
        if (event->id == RE1_SW) {
            codex_long_press_mask |= mask;
            codex_set_page(CODEX_UI_SETTINGS);
        } else if (event->id == RE2_SW) {
            codex_long_press_mask |= mask;
            codex_set_page(CODEX_UI_OVERVIEW);
        } else if (event->id == 19) {
            codex_long_press_mask |= mask;
            codex_set_page(CODEX_UI_DIAGNOSTICS);
        }
        return;
    }
    if (event->type == SW_EVENT_RELEASE &&
        (codex_long_press_mask & mask) != 0) {
        codex_long_press_mask &= ~mask;
        return;
    }
    local = codex_controls_local(event->id);
    if (local != CODEX_LOCAL_NONE) {
        codex_handle_local_key(local, event->type);
        return;
    }
    if (event->id == RE1_SW || event->id == RE2_SW) {
        codex_handle_encoder_switch(event);
        return;
    }
    if (codex_controls_translate(event->id, event->type, &control)) {
        codex_notify_control(&control);
        if (control.agent >= 0 && control.action == 1) {
            codex_set_selected((unsigned)control.agent);
            codex_render_current();
        } else if (control.action == 1) {
            codex_set_context("Codex action");
            codex_render_current();
        }
    }
}

static void codex_controls_task(void *unused)
{
    switch_event_t event;
    rotary_encoder_event_t encoder;
    codex_control_event_t control;
    (void)unused;
    for (;;) {
        if (xQueueReceive(rotary_encoder_event_queue, &encoder, 0) == pdTRUE) {
            uint8_t clockwise =
                encoder.state.direction == ROTARY_ENCODER_DIRECTION_CLOCKWISE;
            codex_note_local_input(
                encoder.state.id == ROTARY_ENCODER_UPPER ?
                    (clockwise ? RE1_CW : RE1_CCW) :
                    (clockwise ? RE2_CW : RE2_CCW),
                SW_EVENT_SHORT_PRESS);
            if (encoder.state.id == ROTARY_ENCODER_UPPER) {
                if (codex_ui_page == CODEX_UI_SETTINGS) {
                    codex_adjust_setting(clockwise ? 1 : -1, 0);
                } else if (codex_ui_page != CODEX_UI_DIAGNOSTICS &&
                           codex_controls_translate_encoder(clockwise,
                                                            &control)) {
                    codex_notify_control(&control);
                    codex_set_context(clockwise ? "Reasoning previous" :
                                                   "Reasoning next");
                    codex_render_current();
                }
            } else if (encoder.state.id == ROTARY_ENCODER_LOWER) {
                if (codex_ui_page == CODEX_UI_SETTINGS ||
                    codex_ui_page == CODEX_UI_DIAGNOSTICS) {
                    codex_navigate_local(clockwise ? 1 : -1);
                } else {
                    codex_select_relative(clockwise);
                }
            }
        }
        if (xQueueReceive(switch_event_queue, &event, pdMS_TO_TICKS(10)) ==
            pdTRUE)
            codex_handle_switch(&event);
    }
}

static void codex_render_led_test(uint32_t tick,
                                  codex_rgb_t output[CODEX_LIGHT_LED_COUNT])
{
    unsigned index;
    for (index = 0; index < CODEX_LIGHT_LED_COUNT; ++index) {
        unsigned phase = (index + tick / 3u) % CODEX_LIGHT_LED_COUNT;
        output[index] = (codex_rgb_t){
            .red = phase < 7 ? 96 : 0,
            .green = phase >= 7 && phase < 14 ? 96 : 0,
            .blue = phase >= 14 ? 96 : 0,
        };
    }
}

static void codex_lighting_task(void *unused)
{
    codex_rgb_t pixels[CODEX_LIGHT_LED_COUNT];
    uint32_t tick = 0;
    (void)unused;
    for (;;) {
        if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (codex_led_test_active)
                codex_render_led_test(tick, pixels);
            else
                codex_lighting_render(&codex_lighting, tick, pixels);
            xSemaphoreGive(codex_state_mutex);
            for (unsigned index = 0; index < NEOPIXEL_COUNT; ++index)
                set_pixel_3color_update_buffer(index, pixels[index].red,
                                               pixels[index].green,
                                               pixels[index].blue);
            neopixel_draw_current_buffer();
        }
        ++tick;
        vTaskDelay(pdMS_TO_TICKS(CODEX_LIGHTING_FRAME_MS));
    }
}

static void codex_system_task(void *unused)
{
    TickType_t last_refresh = 0;
    int mounted_last = -1;
    (void)unused;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        codex_settings_t settings_to_save;
        uint8_t should_save = 0;
        uint8_t should_render = 0;
        int mounted = tud_mounted() ? 1 : 0;

        if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(30)) == pdTRUE) {
            uint32_t idle_ms = ticks_to_ms(now - codex_last_input_tick);
            uint8_t next_screen = 0;
            if (codex_settings.oled_timeout_seconds != 0) {
                uint32_t timeout_ms =
                    (uint32_t)codex_settings.oled_timeout_seconds * 1000u;
                if (idle_ms >= timeout_ms)
                    next_screen = 2;
                else if (codex_settings.auto_dim &&
                         idle_ms >= timeout_ms / 2u)
                    next_screen = 1;
            }
            if (next_screen != codex_screen_state) {
                codex_screen_state = next_screen;
                should_render = 1;
            }
            if (codex_settings.lighting_timeout_seconds != 0 &&
                idle_ms >=
                    (uint32_t)codex_settings.lighting_timeout_seconds * 1000u)
                codex_lighting_set_output_enabled(&codex_lighting, 0);
            if (codex_settings_dirty &&
                ticks_to_ms(now - codex_settings_changed_tick) >=
                    CODEX_SETTINGS_SAVE_DELAY_MS) {
                settings_to_save = codex_settings;
                codex_settings_dirty = 0;
                should_save = 1;
            }
            if (mounted != mounted_last) {
                snprintf(codex_context, sizeof(codex_context), "%s",
                         mounted ? "Codex USB connected" :
                                   "Waiting for Codex USB");
                should_render = 1;
            }
            xSemaphoreGive(codex_state_mutex);
        }
        mounted_last = mounted;
        if (should_save && !codex_settings_save(&settings_to_save)) {
            codex_set_error("Settings save failed");
            if (xSemaphoreTake(codex_state_mutex, pdMS_TO_TICKS(20)) ==
                pdTRUE) {
                codex_settings_dirty = 1;
                codex_settings_changed_tick = now;
                xSemaphoreGive(codex_state_mutex);
            }
        }
        if (should_render ||
            ticks_to_ms(now - last_refresh) >= CODEX_UI_REFRESH_MS) {
            codex_render_current();
            last_refresh = now;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void codex_mode_receive_hid(const uint8_t *payload, size_t payload_length)
{
    codex_rx_item_t item = {0};
    if (codex_rx_queue == NULL || payload == NULL ||
        payload_length > CODEX_HID_REPORT_SIZE - 1)
        return;
    item.report[0] = CODEX_HID_REPORT_ID;
    memcpy(item.report + 1, payload, payload_length);
    if (xQueueSend(codex_rx_queue, &item, 0) != pdTRUE)
        ++codex_rx_dropped;
}

void codex_mode_run(void)
{
    codex_rx_queue = xQueueCreate(CODEX_RX_QUEUE_DEPTH, sizeof(codex_rx_item_t));
    codex_state_mutex = xSemaphoreCreateMutex();
    codex_oled_mutex = xSemaphoreCreateMutex();
    if (codex_rx_queue == NULL || codex_state_mutex == NULL ||
        codex_oled_mutex == NULL || !codex_hid_init()) {
        ESP_LOGE(TAG, "Unable to allocate Codex runtime state");
        return;
    }
    codex_settings_load(&codex_settings);
    codex_lighting_init(&codex_lighting);
    codex_lighting_set_master_brightness(
        &codex_lighting, codex_settings.led_brightness_percent);
    codex_lighting_set_animation_speed(
        &codex_lighting, codex_settings.animation_speed_percent);
    codex_lighting_set_selected(&codex_lighting, 0);
    codex_last_input_tick = xTaskGetTickCount();
    mount_hid_only();
    neopixel_off();
    codex_render_current();
    xTaskCreate(kb_scan_task, "codex_scan", SW_SCAN_TASK_STACK_SIZE,
                NULL, 5, NULL);
    xTaskCreate(codex_rpc_task, "codex_rpc", CODEX_TASK_STACK_SIZE,
                NULL, 6, NULL);
    xTaskCreate(codex_controls_task, "codex_controls", 5120,
                NULL, 5, NULL);
    xTaskCreate(codex_lighting_task, "codex_lighting", 3072,
                NULL, 5, NULL);
    xTaskCreate(codex_system_task, "codex_system", 4096,
                NULL, 4, NULL);
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}
