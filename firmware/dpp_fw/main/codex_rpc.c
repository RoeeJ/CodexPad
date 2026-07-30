#include "codex_rpc.h"

#include <stdbool.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#include "cJSON.h"

#define CODEX_RPC_PARSE_ERROR_CODE -32700
#define CODEX_RPC_INVALID_REQUEST_CODE -32600
#define CODEX_RPC_METHOD_NOT_FOUND_CODE -32601
#define CODEX_RPC_INVALID_PARAMS_CODE -32602
#define CODEX_RPC_INTERNAL_ERROR_CODE -32603

static void set_result(codex_rpc_result_t *output, codex_rpc_result_t value)
{
    if (output != NULL)
        *output = value;
}

static cJSON *response_with_id(const cJSON *id)
{
    cJSON *response = cJSON_CreateObject();
    if (response == NULL)
        return NULL;
    if (id != NULL)
        cJSON_AddItemToObject(response, "id", cJSON_Duplicate(id, true));
    else
        cJSON_AddNullToObject(response, "id");
    return response;
}

static char *print_response(cJSON *response)
{
    char *json;
    if (response == NULL)
        return NULL;
    json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return json;
}

static char *error_response(const cJSON *id, int code, const char *message)
{
    cJSON *response = response_with_id(id);
    cJSON *error;
    if (response == NULL)
        return NULL;
    error = cJSON_AddObjectToObject(response, "error");
    if (error == NULL) {
        cJSON_Delete(response);
        return NULL;
    }
    cJSON_AddNumberToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    return print_response(response);
}

static int valid_id(const cJSON *id)
{
    double value;
    if (!cJSON_IsNumber(id))
        return 0;
    value = id->valuedouble;
    return isfinite(value) && value >= 0.0 && value <= 998.0 &&
           floor(value) == value;
}

static int null_or_absent(const cJSON *value)
{
    return value == NULL || cJSON_IsNull(value);
}

static int number_in_range(const cJSON *item, double minimum, double maximum)
{
    return cJSON_IsNumber(item) && isfinite(item->valuedouble) &&
           item->valuedouble >= minimum && item->valuedouble <= maximum;
}

static int light_is_valid(const cJSON *entry)
{
    cJSON *color;
    cJSON *brightness;
    cJSON *effect;
    cJSON *speed;

    if (!cJSON_IsObject(entry))
        return 0;
    color = cJSON_GetObjectItemCaseSensitive(entry, "c");
    brightness = cJSON_GetObjectItemCaseSensitive(entry, "b");
    effect = cJSON_GetObjectItemCaseSensitive(entry, "e");
    speed = cJSON_GetObjectItemCaseSensitive(entry, "s");
    if (color != NULL && !number_in_range(color, 0.0, 16777215.0))
        return 0;
    if (brightness != NULL && !number_in_range(brightness, 0.0, 1.0))
        return 0;
    if (effect != NULL &&
        !number_in_range(effect, CODEX_EFFECT_OFF,
                         CODEX_EFFECT_SHALLOW_BREATH))
        return 0;
    if (speed != NULL && !number_in_range(speed, 0.0, 1.0))
        return 0;
    return 1;
}

static codex_light_config_t parse_light(const cJSON *entry)
{
    const cJSON *color = cJSON_GetObjectItemCaseSensitive(entry, "c");
    const cJSON *brightness = cJSON_GetObjectItemCaseSensitive(entry, "b");
    const cJSON *effect = cJSON_GetObjectItemCaseSensitive(entry, "e");
    const cJSON *speed = cJSON_GetObjectItemCaseSensitive(entry, "s");
    codex_light_config_t config = {
        .color = cJSON_IsNumber(color) ? (uint32_t)color->valuedouble : 0,
        .brightness =
            cJSON_IsNumber(brightness) ?
                (uint8_t)(brightness->valuedouble * 255.0 + 0.5) : 255,
        .effect = cJSON_IsNumber(effect) ? (uint8_t)effect->valueint :
                                          CODEX_EFFECT_SOLID,
        .speed = cJSON_IsNumber(speed) ?
                     (uint8_t)(speed->valuedouble * 255.0 + 0.5) : 255,
    };
    return config;
}

static char *handle_version(const cJSON *id, const cJSON *params,
                            codex_rpc_result_t *result)
{
    cJSON *response;
    cJSON *body;
    if (!null_or_absent(params)) {
        set_result(result, CODEX_RPC_INVALID_PARAMS);
        return error_response(id, CODEX_RPC_INVALID_PARAMS_CODE,
                              "Invalid params");
    }
    response = response_with_id(id);
    if (response == NULL)
        return NULL;
    body = cJSON_AddObjectToObject(response, "result");
    if (body == NULL) {
        cJSON_Delete(response);
        return NULL;
    }
    cJSON_AddStringToObject(body, "version", CODEX_PROTOCOL_VERSION);
    return print_response(response);
}

static char *handle_status(const cJSON *id, const cJSON *params,
                           const codex_rpc_callbacks_t *callbacks,
                           codex_rpc_result_t *result)
{
    codex_rpc_diagnostics_t diagnostics = {0};
    cJSON *response;
    cJSON *body;
    cJSON *details;

    if (!null_or_absent(params)) {
        set_result(result, CODEX_RPC_INVALID_PARAMS);
        return error_response(id, CODEX_RPC_INVALID_PARAMS_CODE,
                              "Invalid params");
    }
    if (callbacks != NULL && callbacks->read_diagnostics != NULL)
        callbacks->read_diagnostics(callbacks->context, &diagnostics);
    response = response_with_id(id);
    if (response == NULL)
        return NULL;
    body = cJSON_AddObjectToObject(response, "result");
    if (body == NULL) {
        cJSON_Delete(response);
        return NULL;
    }
    cJSON_AddStringToObject(body, "version", CODEX_PROTOCOL_VERSION);
    cJSON_AddNumberToObject(body, "profile_index", 0);
    cJSON_AddNumberToObject(body, "layer_index", 0);
    cJSON_AddNumberToObject(body, "selected_agent",
                            diagnostics.selected_agent);
    cJSON_AddBoolToObject(body, "usb_connected",
                          diagnostics.usb_connected != 0);
    details = cJSON_AddObjectToObject(body, "diagnostics");
    if (details != NULL) {
        cJSON_AddNumberToObject(details, "rx_dropped",
                                diagnostics.rx_dropped);
        cJSON_AddNumberToObject(details, "rx_rejected",
                                diagnostics.rx_rejected);
        cJSON_AddNumberToObject(details, "rx_timeouts",
                                diagnostics.rx_timeouts);
        cJSON_AddNumberToObject(details, "tx_failed", diagnostics.tx_failed);
        cJSON_AddNumberToObject(details, "rpc_requests",
                                diagnostics.rpc_requests);
        cJSON_AddNumberToObject(details, "input_dropped",
                                diagnostics.input_dropped);
        cJSON_AddNumberToObject(details, "uptime_ms", diagnostics.uptime_ms);
        cJSON_AddNumberToObject(details, "free_memory",
                                diagnostics.current_free_memory);
        cJSON_AddNumberToObject(details, "minimum_free_memory",
                                diagnostics.minimum_free_memory);
        cJSON_AddStringToObject(details, "last_error",
                                diagnostics.last_error != NULL ?
                                    diagnostics.last_error : "");
    }
    return print_response(response);
}

static char *handle_rgb(const cJSON *id, const cJSON *params,
                        const codex_rpc_callbacks_t *callbacks,
                        codex_rpc_result_t *result)
{
    const cJSON *ambient;
    const cJSON *keys;
    const cJSON *chosen;
    cJSON *response;

    if (!cJSON_IsObject(params)) {
        set_result(result, CODEX_RPC_INVALID_PARAMS);
        return error_response(id, CODEX_RPC_INVALID_PARAMS_CODE,
                              "Invalid params");
    }
    ambient = cJSON_GetObjectItemCaseSensitive(params, "ambient");
    keys = cJSON_GetObjectItemCaseSensitive(params, "keys");
    chosen = cJSON_IsObject(ambient) ? ambient : keys;
    if (chosen == NULL || !light_is_valid(chosen) ||
        (ambient != NULL && !light_is_valid(ambient)) ||
        (keys != NULL && !light_is_valid(keys))) {
        set_result(result, CODEX_RPC_INVALID_PARAMS);
        return error_response(id, CODEX_RPC_INVALID_PARAMS_CODE,
                              "Invalid params");
    }
    if (callbacks != NULL && callbacks->apply_ambient != NULL)
        callbacks->apply_ambient(callbacks->context, parse_light(chosen));
    response = response_with_id(id);
    if (response != NULL)
        cJSON_AddBoolToObject(response, "result", true);
    return print_response(response);
}

static int thread_array_is_valid(const cJSON *params)
{
    const cJSON *entry;
    if (!cJSON_IsArray(params))
        return 0;
    cJSON_ArrayForEach(entry, params) {
        const cJSON *id;
        if (!cJSON_IsObject(entry) || !light_is_valid(entry))
            return 0;
        id = cJSON_GetObjectItemCaseSensitive(entry, "id");
        if (!number_in_range(id, 0.0, 5.0) ||
            floor(id->valuedouble) != id->valuedouble)
            return 0;
    }
    return 1;
}

static char *handle_threads(const cJSON *id, const cJSON *params,
                            const codex_rpc_callbacks_t *callbacks,
                            codex_rpc_result_t *result)
{
    const cJSON *entry;
    cJSON *response;

    if (!thread_array_is_valid(params)) {
        set_result(result, CODEX_RPC_INVALID_PARAMS);
        return error_response(id, CODEX_RPC_INVALID_PARAMS_CODE,
                              "Invalid params");
    }
    cJSON_ArrayForEach(entry, params) {
        const cJSON *slot = cJSON_GetObjectItemCaseSensitive(entry, "id");
        if (callbacks != NULL && callbacks->apply_thread != NULL)
            callbacks->apply_thread(callbacks->context,
                                    (unsigned)slot->valueint,
                                    parse_light(entry));
        if (callbacks != NULL && callbacks->thread_updated != NULL)
            callbacks->thread_updated(callbacks->context,
                                      (unsigned)slot->valueint);
    }
    response = response_with_id(id);
    if (response != NULL)
        cJSON_AddBoolToObject(response, "result", true);
    return print_response(response);
}

char *codex_rpc_handle(const uint8_t *message, size_t length,
                       const codex_rpc_callbacks_t *callbacks,
                       codex_rpc_result_t *result)
{
    cJSON *request;
    cJSON *method;
    cJSON *params;
    cJSON *id;
    const char *parse_end = NULL;
    char *response;

    set_result(result, CODEX_RPC_OK);
    if (message == NULL || length == 0) {
        set_result(result, CODEX_RPC_PARSE_ERROR);
        return error_response(NULL, CODEX_RPC_PARSE_ERROR_CODE, "Parse error");
    }
    request = cJSON_ParseWithLengthOpts((const char *)message, length,
                                        &parse_end, false);
    if (request == NULL) {
        set_result(result, CODEX_RPC_PARSE_ERROR);
        return error_response(NULL, CODEX_RPC_PARSE_ERROR_CODE, "Parse error");
    }
    while (parse_end < (const char *)message + length &&
           isspace((unsigned char)*parse_end))
        ++parse_end;
    if (parse_end != (const char *)message + length ||
        !cJSON_IsObject(request)) {
        cJSON_Delete(request);
        set_result(result, CODEX_RPC_INVALID_REQUEST);
        return error_response(NULL, CODEX_RPC_INVALID_REQUEST_CODE,
                              "Invalid Request");
    }
    method = cJSON_GetObjectItemCaseSensitive(request, "method");
    params = cJSON_GetObjectItemCaseSensitive(request, "params");
    id = cJSON_GetObjectItemCaseSensitive(request, "id");
    if (!cJSON_IsString(method) || !valid_id(id)) {
        cJSON_Delete(request);
        set_result(result, CODEX_RPC_INVALID_REQUEST);
        return error_response(NULL, CODEX_RPC_INVALID_REQUEST_CODE,
                              "Invalid Request");
    }

    if (strcmp(method->valuestring, "sys.version") == 0)
        response = handle_version(id, params, result);
    else if (strcmp(method->valuestring, "device.status") == 0)
        response = handle_status(id, params, callbacks, result);
    else if (strcmp(method->valuestring, "v.oai.rgbcfg") == 0)
        response = handle_rgb(id, params, callbacks, result);
    else if (strcmp(method->valuestring, "v.oai.thstatus") == 0)
        response = handle_threads(id, params, callbacks, result);
    else {
        set_result(result, CODEX_RPC_METHOD_NOT_FOUND);
        response = error_response(id, CODEX_RPC_METHOD_NOT_FOUND_CODE,
                                  "Method not found");
    }
    cJSON_Delete(request);
    if (response == NULL) {
        set_result(result, CODEX_RPC_INTERNAL_ERROR);
        return error_response(NULL, CODEX_RPC_INTERNAL_ERROR_CODE,
                              "Internal error");
    }
    return response;
}

char *codex_rpc_build_hid_notification(
    const codex_control_event_t *control)
{
    cJSON *notification;
    cJSON *params;
    char *json;
    if (control == NULL || control->key[0] == '\0')
        return NULL;
    notification = cJSON_CreateObject();
    if (notification == NULL)
        return NULL;
    cJSON_AddStringToObject(notification, "method", "v.oai.hid");
    params = cJSON_AddObjectToObject(notification, "params");
    if (params == NULL) {
        cJSON_Delete(notification);
        return NULL;
    }
    cJSON_AddStringToObject(params, "k", control->key);
    cJSON_AddNumberToObject(params, "act", control->action);
    if (control->agent >= 0)
        cJSON_AddNumberToObject(params, "ag", control->agent);
    json = cJSON_PrintUnformatted(notification);
    cJSON_Delete(notification);
    return json;
}

void codex_rpc_free(char *json)
{
    cJSON_free(json);
}

const char *codex_rpc_result_name(codex_rpc_result_t result)
{
    switch (result) {
    case CODEX_RPC_OK:
        return "";
    case CODEX_RPC_PARSE_ERROR:
        return "RPC parse error";
    case CODEX_RPC_INVALID_REQUEST:
        return "RPC invalid request";
    case CODEX_RPC_INVALID_PARAMS:
        return "RPC invalid params";
    case CODEX_RPC_METHOD_NOT_FOUND:
        return "RPC method not found";
    case CODEX_RPC_INTERNAL_ERROR:
    default:
        return "RPC internal error";
    }
}
