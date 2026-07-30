#include "codex_hid.h"

#include <string.h>

#include "class/hid/hid_device.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tusb.h"

#include "codex_wire.h"

#define CODEX_HID_READY_RETRIES 100u

static SemaphoreHandle_t codex_hid_tx_mutex;

int codex_hid_init(void)
{
    if (codex_hid_tx_mutex == NULL)
        codex_hid_tx_mutex = xSemaphoreCreateMutex();
    return codex_hid_tx_mutex != NULL;
}

static int send_bytes_unlocked(const uint8_t *bytes, size_t length)
{
    uint8_t report[CODEX_HID_REPORT_SIZE];
    size_t offset = 0;

    if (bytes == NULL && length != 0)
        return 0;
    while (offset < length) {
        size_t chunk = length - offset;
        if (chunk > CODEX_HID_PAYLOAD_SIZE)
            chunk = CODEX_HID_PAYLOAD_SIZE;
        if (codex_wire_encode_report(
                CODEX_HID_CHANNEL_RPC, bytes + offset, chunk, report) !=
            CODEX_WIRE_OK)
            return 0;
        for (unsigned retry = 0;
             retry < CODEX_HID_READY_RETRIES && !tud_hid_ready(); ++retry)
            vTaskDelay(pdMS_TO_TICKS(1));
        if (!tud_hid_ready() ||
            !tud_hid_report(CODEX_HID_REPORT_ID, report + 1,
                            CODEX_HID_REPORT_SIZE - 1))
            return 0;
        offset += chunk;
    }
    return 1;
}

int codex_hid_send_bytes(const uint8_t *bytes, size_t length)
{
    int result;
    if (codex_hid_tx_mutex == NULL ||
        xSemaphoreTake(codex_hid_tx_mutex, pdMS_TO_TICKS(250)) != pdTRUE)
        return 0;
    result = send_bytes_unlocked(bytes, length);
    xSemaphoreGive(codex_hid_tx_mutex);
    return result;
}

int codex_hid_send_json(const char *json)
{
    int result;
    if (json == NULL)
        return 0;
    if (codex_hid_tx_mutex == NULL ||
        xSemaphoreTake(codex_hid_tx_mutex, pdMS_TO_TICKS(250)) != pdTRUE)
        return 0;
    result = send_bytes_unlocked((const uint8_t *)json, strlen(json)) &&
             send_bytes_unlocked((const uint8_t *)"\n", 1);
    xSemaphoreGive(codex_hid_tx_mutex);
    return result;
}
