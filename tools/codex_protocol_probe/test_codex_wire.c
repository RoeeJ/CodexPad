#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "codex_wire.h"
#include "codex_controls.h"
#include "codex_lighting.h"
#include "codex_settings.h"

static void test_round_trip_boundaries(void)
{
    uint8_t payload[CODEX_HID_PAYLOAD_SIZE];
    uint8_t report[CODEX_HID_REPORT_SIZE];
    const uint8_t *decoded = NULL;
    size_t decoded_length = 0;
    uint8_t channel = 0;
    size_t index;
    size_t length;

    for (index = 0; index < sizeof(payload); ++index)
        payload[index] = (uint8_t)index;

    for (length = 0; length <= sizeof(payload); ++length) {
        assert(codex_wire_encode_report(
            CODEX_HID_CHANNEL_RPC, payload, length, report
        ) == CODEX_WIRE_OK);
        assert(codex_wire_decode_report(
            report, &channel, &decoded, &decoded_length
        ) == CODEX_WIRE_OK);
        assert(channel == CODEX_HID_CHANNEL_RPC);
        assert(decoded_length == length);
        assert(memcmp(decoded, payload, length) == 0);
    }
}

static void test_reassembly(void)
{
    static const uint8_t first[] =
        "{\"method\":\"sys.version\",\"params\":null,";
    static const uint8_t second[] = "\"id\":101}\r\n";
    uint8_t storage[128];
    uint8_t report[CODEX_HID_REPORT_SIZE];
    codex_reassembler_t reassembler;
    const uint8_t *message = NULL;
    size_t message_length = 0;

    codex_reassembler_init(&reassembler, storage, sizeof(storage));
    assert(codex_wire_encode_report(
        CODEX_HID_CHANNEL_RPC, first, sizeof(first) - 1, report
    ) == CODEX_WIRE_OK);
    assert(codex_reassembler_feed(
        &reassembler, report, &message, &message_length
    ) == CODEX_WIRE_OK);
    assert(message == NULL);

    assert(codex_wire_encode_report(
        CODEX_HID_CHANNEL_RPC, second, sizeof(second) - 1, report
    ) == CODEX_WIRE_OK);
    assert(codex_reassembler_feed(
        &reassembler, report, &message, &message_length
    ) == CODEX_WIRE_MESSAGE_COMPLETE);
    assert(message_length == sizeof(first) + sizeof(second) - 4);
    assert(memcmp(message, first, sizeof(first) - 1) == 0);
}

static void test_unterminated_host_json(void)
{
    static const uint8_t message[] =
        "{\"method\":\"sys.version\",\"params\":null,\"id\":7}";
    uint8_t storage[128];
    uint8_t report[CODEX_HID_REPORT_SIZE];
    codex_reassembler_t reassembler;
    const uint8_t *decoded = NULL;
    size_t decoded_length = 0;

    codex_reassembler_init(&reassembler, storage, sizeof(storage));
    assert(codex_wire_encode_report(CODEX_HID_CHANNEL_RPC, message,
                                    sizeof(message) - 1, report) ==
           CODEX_WIRE_OK);
    assert(codex_reassembler_feed(&reassembler, report, &decoded,
                                  &decoded_length) ==
           CODEX_WIRE_MESSAGE_COMPLETE);
    assert(decoded_length == sizeof(message) - 1);

    /* Exact full-report JSON boundary, including braces inside a string. */
    static const uint8_t boundary[] =
        "{\"v\":\"a}b\",\"padding\":\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"}";
    assert(sizeof(boundary) - 1 == CODEX_HID_PAYLOAD_SIZE);
    codex_reassembler_reset(&reassembler);
    assert(codex_wire_encode_report(CODEX_HID_CHANNEL_RPC, boundary,
                                    sizeof(boundary) - 1, report) ==
           CODEX_WIRE_OK);
    assert(codex_reassembler_feed(&reassembler, report, &decoded,
                                  &decoded_length) ==
           CODEX_WIRE_MESSAGE_COMPLETE);
}

static void test_reassembly_overflow_resets(void)
{
    uint8_t storage[8];
    uint8_t report[CODEX_HID_REPORT_SIZE];
    uint8_t payload[9] = {0};
    codex_reassembler_t reassembler;
    const uint8_t *message = NULL;
    size_t message_length = 0;

    codex_reassembler_init(&reassembler, storage, sizeof(storage));
    assert(codex_wire_encode_report(
        CODEX_HID_CHANNEL_RPC, payload, sizeof(payload), report
    ) == CODEX_WIRE_OK);
    assert(codex_reassembler_feed(
        &reassembler, report, &message, &message_length
    ) == CODEX_WIRE_OVERFLOW);
    assert(reassembler.length == 0);
}

static void test_invalid_reports(void)
{
    uint8_t report[CODEX_HID_REPORT_SIZE] = {0};
    const uint8_t *payload = NULL;
    size_t payload_length = 0;
    uint8_t channel = 0;

    report[0] = 5;
    report[1] = CODEX_HID_CHANNEL_RPC;
    assert(codex_wire_decode_report(
        report, &channel, &payload, &payload_length
    ) == CODEX_WIRE_INVALID_REPORT);

    report[0] = CODEX_HID_REPORT_ID;
    report[1] = 99;
    assert(codex_wire_decode_report(
        report, &channel, &payload, &payload_length
    ) == CODEX_WIRE_INVALID_CHANNEL);

    report[1] = CODEX_HID_CHANNEL_RPC;
    report[2] = CODEX_HID_PAYLOAD_SIZE + 1;
    assert(codex_wire_decode_report(
        report, &channel, &payload, &payload_length
    ) == CODEX_WIRE_INVALID_REPORT);
}

static void test_control_edges(void)
{
    codex_control_event_t event;
    unsigned slot;
    for (slot = 0; slot < 6; ++slot) {
        assert(codex_controls_translate((uint8_t)slot, 1, &event));
        assert(event.key[0] == 'A' && event.key[1] == 'G');
        assert(event.key[2] == '0' && event.key[3] == (char)('0' + slot));
        assert(event.action == 1);
        assert(event.agent == (int8_t)slot);
        assert(codex_controls_translate((uint8_t)slot, 0, &event));
        assert(event.action == 0);
    }
    assert(codex_controls_translate(6, 1, &event));
    assert(strcmp(event.key, "ACT06") == 0 && event.agent == -1);
    assert(codex_controls_translate(10, 0, &event));
    assert(strcmp(event.key, "ACT10") == 0 && event.action == 0);
    assert(codex_controls_translate(11, 1, &event));
    assert(strcmp(event.key, "ACT12") == 0);
    assert(codex_controls_translate(22, 1, &event));
    assert(strcmp(event.key, "ENC_TO") == 0);
    assert(codex_controls_translate(12, 1, &event));
    assert(strcmp(event.key, "ACT06") == 0);
    assert(codex_controls_translate(13, 1, &event));
    assert(strcmp(event.key, "ACT07") == 0);
    assert(codex_controls_translate(14, 1, &event));
    assert(strcmp(event.key, "ACT08") == 0);
    assert(codex_controls_translate(15, 1, &event));
    assert(strcmp(event.key, "ACT09") == 0);
    assert(!codex_controls_translate(16, 1, &event));
    assert(!codex_controls_translate(0, 2, &event));
    assert(!codex_controls_translate(0, 1, NULL));
    assert(codex_controls_local(15) == CODEX_LOCAL_NONE);
    assert(codex_controls_local(16) == CODEX_LOCAL_STATUS);
    assert(codex_controls_local(17) == CODEX_LOCAL_DIMMER);
    assert(codex_controls_local(18) == CODEX_LOCAL_BRIGHTER);
    assert(codex_controls_local(19) == CODEX_LOCAL_SETTINGS);
    assert(codex_controls_translate_encoder(1, &event));
    assert(strcmp(event.key, "ENC_CW") == 0 && event.action == 2);
    assert(codex_controls_translate_encoder(0, &event));
    assert(strcmp(event.key, "ENC_CC") == 0 && event.action == 2);
}

static int rgb_equal(codex_rgb_t left, codex_rgb_t right)
{
    return left.red == right.red && left.green == right.green &&
           left.blue == right.blue;
}

static void test_lighting_zones_and_effects(void)
{
    codex_lighting_t model;
    codex_rgb_t first[CODEX_LIGHT_LED_COUNT];
    codex_rgb_t later[CODEX_LIGHT_LED_COUNT];
    codex_light_config_t ambient = {
        .color = 0x804020,
        .brightness = 255,
        .effect = CODEX_EFFECT_SOLID,
        .speed = 255,
    };
    codex_light_config_t thread = {
        .color = 0xff0000,
        .brightness = 255,
        .effect = CODEX_EFFECT_SOLID,
        .speed = 255,
    };
    unsigned effect;
    unsigned index;

    codex_lighting_init(&model);
    codex_lighting_set_ambient(&model, ambient);
    assert(codex_lighting_set_thread(&model, 2, thread));
    assert(!codex_lighting_set_thread(&model, 6, thread));
    codex_lighting_render(&model, 0, first);
    assert(first[2].red == 255 && first[2].green == 0 &&
           first[2].blue == 0);
    assert(first[6].red == 128 && first[6].green == 64 &&
           first[6].blue == 32);
    assert(first[0].red == 0 && first[0].green == 0 && first[0].blue == 0);

    codex_lighting_set_selected(&model, 2);
    codex_lighting_render(&model, 0, first);
    assert(first[2].green > 0 && first[2].blue > 0);
    codex_lighting_set_master_brightness(&model, 50);
    codex_lighting_render(&model, 0, first);
    assert(first[2].red <= 128);
    codex_lighting_set_output_enabled(&model, 0);
    codex_lighting_render(&model, 0, first);
    for (index = 0; index < CODEX_LIGHT_LED_COUNT; ++index)
        assert(first[index].red == 0 && first[index].green == 0 &&
               first[index].blue == 0);
    codex_lighting_set_output_enabled(&model, 1);
    codex_lighting_set_master_brightness(&model, 100);
    codex_lighting_set_selected(&model, -1);

    ambient.effect = CODEX_EFFECT_RAINBOW;
    codex_lighting_set_ambient(&model, ambient);
    codex_lighting_set_animation_speed(&model, 0);
    codex_lighting_render(&model, 0, first);
    codex_lighting_render(&model, 73, later);
    for (index = 0; index < CODEX_LIGHT_LED_COUNT; ++index)
        assert(rgb_equal(first[index], later[index]));
    codex_lighting_set_animation_speed(&model, 100);
    codex_lighting_render(&model, 73, later);
    for (index = 6; index < CODEX_LIGHT_LED_COUNT; ++index) {
        if (!rgb_equal(first[index], later[index]))
            break;
    }
    assert(index < CODEX_LIGHT_LED_COUNT);

    for (effect = CODEX_EFFECT_SNAKE;
         effect <= CODEX_EFFECT_SHALLOW_BREATH; ++effect) {
        ambient.effect = (uint8_t)effect;
        codex_lighting_set_ambient(&model, ambient);
        codex_lighting_render(&model, 0, first);
        codex_lighting_render(&model, 73, later);
        for (index = 6; index < CODEX_LIGHT_LED_COUNT; ++index) {
            if (!rgb_equal(first[index], later[index]))
                break;
        }
        assert(index < CODEX_LIGHT_LED_COUNT);
    }
}

static void test_settings_model(void)
{
    codex_settings_t settings;
    codex_settings_defaults(&settings);
    assert(settings.version == CODEX_SETTINGS_VERSION);
    assert(settings.led_brightness_percent == 100);
    assert(settings.oled_contrast == 255);
    assert(settings.auto_dim == 1);
    assert(settings.animation_speed_percent == 100);
    assert(settings.boot_codex == 1);
    assert(settings.oled_timeout_seconds == 60);
    assert(settings.lighting_timeout_seconds == 300);

    settings.led_brightness_percent = 255;
    settings.animation_speed_percent = 200;
    settings.auto_dim = 4;
    settings.boot_codex = 9;
    settings.oled_timeout_seconds = 65000;
    settings.lighting_timeout_seconds = 65000;
    codex_settings_sanitize(&settings);
    assert(settings.led_brightness_percent == 100);
    assert(settings.animation_speed_percent == 100);
    assert(settings.auto_dim == 1);
    assert(settings.boot_codex == 1);
    assert(settings.oled_timeout_seconds == 600);
    assert(settings.lighting_timeout_seconds == 3600);
}

int main(void)
{
    test_round_trip_boundaries();
    test_reassembly();
    test_unterminated_host_json();
    test_reassembly_overflow_resets();
    test_invalid_reports();
    test_control_edges();
    test_lighting_zones_and_effects();
    test_settings_model();
    return 0;
}
