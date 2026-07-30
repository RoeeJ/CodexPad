#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "codex_rpc.h"

typedef struct {
    unsigned ambient_updates;
    unsigned thread_updates;
    unsigned last_slot;
    codex_light_config_t ambient;
    codex_light_config_t thread;
} test_context_t;

static void apply_ambient(void *context, codex_light_config_t config)
{
    test_context_t *state = context;
    ++state->ambient_updates;
    state->ambient = config;
}

static void apply_thread(void *context, unsigned slot,
                         codex_light_config_t config)
{
    test_context_t *state = context;
    ++state->thread_updates;
    state->last_slot = slot;
    state->thread = config;
}

static void read_diagnostics(void *context,
                             codex_rpc_diagnostics_t *diagnostics)
{
    (void)context;
    *diagnostics = (codex_rpc_diagnostics_t){
        .rx_dropped = 1,
        .rx_rejected = 2,
        .rx_timeouts = 3,
        .tx_failed = 4,
        .rpc_requests = 5,
        .input_dropped = 6,
        .uptime_ms = 6000,
        .current_free_memory = 7000,
        .minimum_free_memory = 5000,
        .usb_connected = 1,
        .selected_agent = 3,
        .last_error = "test error",
    };
}

static cJSON *call(const char *request, const codex_rpc_callbacks_t *callbacks,
                   codex_rpc_result_t expected)
{
    codex_rpc_result_t result = CODEX_RPC_INTERNAL_ERROR;
    char *response = codex_rpc_handle((const uint8_t *)request,
                                      strlen(request), callbacks, &result);
    assert(response != NULL);
    assert(result == expected);
    cJSON *json = cJSON_Parse(response);
    assert(json != NULL);
    codex_rpc_free(response);
    return json;
}

static void assert_error(cJSON *response, int code)
{
    cJSON *error = cJSON_GetObjectItemCaseSensitive(response, "error");
    cJSON *actual = cJSON_GetObjectItemCaseSensitive(error, "code");
    assert(cJSON_IsObject(error));
    assert(cJSON_IsNumber(actual));
    assert(actual->valueint == code);
}

static void test_version_and_ids(const codex_rpc_callbacks_t *callbacks)
{
    cJSON *response = call(
        "{\"method\":\"sys.version\",\"params\":null,\"id\":0}",
        callbacks, CODEX_RPC_OK);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(response, "result");
    cJSON *version = cJSON_GetObjectItemCaseSensitive(result, "version");
    assert(cJSON_IsString(version));
    assert(strcmp(version->valuestring, CODEX_PROTOCOL_VERSION) == 0);
    cJSON_Delete(response);

    response = call(
        "{\"method\":\"sys.version\",\"params\":null,\"id\":998}",
        callbacks, CODEX_RPC_OK);
    assert(cJSON_GetObjectItemCaseSensitive(response, "id")->valueint == 998);
    cJSON_Delete(response);

    response = call("{\"method\":\"sys.version\",\"id\":999}", callbacks,
                    CODEX_RPC_INVALID_REQUEST);
    assert_error(response, -32600);
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(response, "id")));
    cJSON_Delete(response);

    response = call("{\"method\":\"sys.version\"}", callbacks,
                    CODEX_RPC_INVALID_REQUEST);
    assert_error(response, -32600);
    cJSON_Delete(response);

    response = call("{\"method\":\"sys.version\",\"id\":\"1\"}", callbacks,
                    CODEX_RPC_INVALID_REQUEST);
    assert_error(response, -32600);
    cJSON_Delete(response);
}

static void test_errors(const codex_rpc_callbacks_t *callbacks)
{
    cJSON *response = call("{", callbacks, CODEX_RPC_PARSE_ERROR);
    assert_error(response, -32700);
    cJSON_Delete(response);

    response = call(
        "{\"method\":\"sys.version\",\"id\":1} trailing", callbacks,
        CODEX_RPC_INVALID_REQUEST);
    assert_error(response, -32600);
    cJSON_Delete(response);

    response = call(
        "{\"method\":\"sys.version\",\"params\":{},\"id\":1}", callbacks,
        CODEX_RPC_INVALID_PARAMS);
    assert_error(response, -32602);
    cJSON_Delete(response);

    response = call(
        "{\"method\":\"not.real\",\"params\":null,\"id\":2}", callbacks,
        CODEX_RPC_METHOD_NOT_FOUND);
    assert_error(response, -32601);
    cJSON_Delete(response);
}

static void test_status(const codex_rpc_callbacks_t *callbacks)
{
    cJSON *response = call(
        "{\"method\":\"device.status\",\"params\":null,\"id\":3}",
        callbacks, CODEX_RPC_OK);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(response, "result");
    cJSON *diagnostics =
        cJSON_GetObjectItemCaseSensitive(result, "diagnostics");
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(
        result, "usb_connected")));
    assert(cJSON_GetObjectItemCaseSensitive(
               result, "selected_agent")->valueint == 3);
    assert(cJSON_GetObjectItemCaseSensitive(
               diagnostics, "minimum_free_memory")->valueint == 5000);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(
                      diagnostics, "last_error")->valuestring,
                  "test error") == 0);
    cJSON_Delete(response);
}

static void test_lighting(const codex_rpc_callbacks_t *callbacks,
                          test_context_t *state)
{
    cJSON *response = call(
        "{\"method\":\"v.oai.rgbcfg\",\"params\":{\"ambient\":"
        "{\"c\":16711680,\"b\":0.5,\"e\":4,\"s\":0.25}},\"id\":4}",
        callbacks, CODEX_RPC_OK);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "result")));
    assert(state->ambient_updates == 1);
    assert(state->ambient.color == 0xff0000);
    assert(state->ambient.brightness == 128);
    assert(state->ambient.effect == CODEX_EFFECT_BREATH);
    assert(state->ambient.speed == 64);
    cJSON_Delete(response);

    response = call(
        "{\"method\":\"v.oai.rgbcfg\",\"params\":{\"ambient\":"
        "{\"b\":1.1}},\"id\":5}",
        callbacks, CODEX_RPC_INVALID_PARAMS);
    assert_error(response, -32602);
    assert(state->ambient_updates == 1);
    cJSON_Delete(response);

    response = call(
        "{\"method\":\"v.oai.thstatus\",\"params\":["
        "{\"id\":2,\"c\":255,\"b\":1,\"e\":1,\"s\":0},"
        "{\"id\":5,\"c\":65280,\"b\":0.5,\"e\":2,\"s\":1}],\"id\":6}",
        callbacks, CODEX_RPC_OK);
    assert(state->thread_updates == 2);
    assert(state->last_slot == 5);
    cJSON_Delete(response);

    response = call(
        "{\"method\":\"v.oai.thstatus\",\"params\":["
        "{\"id\":1,\"b\":1},{\"id\":6,\"b\":1}],\"id\":7}",
        callbacks, CODEX_RPC_INVALID_PARAMS);
    assert_error(response, -32602);
    assert(state->thread_updates == 2);
    cJSON_Delete(response);
}

static void test_notification(void)
{
    codex_control_event_t control = {
        .key = "AG03",
        .action = 1,
        .agent = 3,
    };
    char *message = codex_rpc_build_hid_notification(&control);
    assert(message != NULL);
    cJSON *json = cJSON_Parse(message);
    cJSON *params = cJSON_GetObjectItemCaseSensitive(json, "params");
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(
                      json, "method")->valuestring,
                  "v.oai.hid") == 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(
                      params, "k")->valuestring,
                  "AG03") == 0);
    assert(cJSON_GetObjectItemCaseSensitive(params, "act")->valueint == 1);
    assert(cJSON_GetObjectItemCaseSensitive(params, "ag")->valueint == 3);
    cJSON_Delete(json);
    codex_rpc_free(message);
}

int main(void)
{
    test_context_t context = {0};
    const codex_rpc_callbacks_t callbacks = {
        .context = &context,
        .apply_ambient = apply_ambient,
        .apply_thread = apply_thread,
        .read_diagnostics = read_diagnostics,
    };
    test_version_and_ids(&callbacks);
    test_errors(&callbacks);
    test_status(&callbacks);
    test_lighting(&callbacks, &context);
    test_notification();
    return 0;
}
