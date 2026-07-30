#ifndef DPP_CODEX_RPC_H
#define DPP_CODEX_RPC_H

#include <stddef.h>
#include <stdint.h>

#include "codex_controls.h"
#include "codex_lighting.h"

#define CODEX_PROTOCOL_VERSION "0.2.4"
#define CODEX_RPC_LAST_ERROR_SIZE 64

typedef struct {
    uint32_t rx_dropped;
    uint32_t rx_rejected;
    uint32_t rx_timeouts;
    uint32_t tx_failed;
    uint32_t rpc_requests;
    uint32_t input_dropped;
    uint32_t uptime_ms;
    uint32_t current_free_memory;
    uint32_t minimum_free_memory;
    uint8_t usb_connected;
    uint8_t selected_agent;
    const char *last_error;
} codex_rpc_diagnostics_t;

typedef struct {
    void *context;
    void (*apply_ambient)(void *context, codex_light_config_t config);
    void (*apply_thread)(void *context, unsigned slot,
                         codex_light_config_t config);
    void (*thread_updated)(void *context, unsigned slot);
    void (*read_diagnostics)(void *context,
                             codex_rpc_diagnostics_t *diagnostics);
} codex_rpc_callbacks_t;

typedef enum {
    CODEX_RPC_OK = 0,
    CODEX_RPC_PARSE_ERROR,
    CODEX_RPC_INVALID_REQUEST,
    CODEX_RPC_INVALID_PARAMS,
    CODEX_RPC_METHOD_NOT_FOUND,
    CODEX_RPC_INTERNAL_ERROR,
} codex_rpc_result_t;

/*
 * Returns a compact, newline-free JSON response allocated by cJSON. The caller
 * must release it with codex_rpc_free(). Parse and request errors still produce
 * a standards-shaped response with a null ID.
 */
char *codex_rpc_handle(const uint8_t *message, size_t length,
                       const codex_rpc_callbacks_t *callbacks,
                       codex_rpc_result_t *result);
char *codex_rpc_build_hid_notification(
    const codex_control_event_t *control);
void codex_rpc_free(char *json);
const char *codex_rpc_result_name(codex_rpc_result_t result);

#endif
