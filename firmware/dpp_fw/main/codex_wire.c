#include "codex_wire.h"

#include <string.h>

static int codex_wire_valid_channel(uint8_t channel)
{
    return channel == CODEX_HID_CHANNEL_DEBUG ||
           channel == CODEX_HID_CHANNEL_RPC;
}

static int codex_wire_json_object_complete(const uint8_t *buffer, size_t length)
{
    int depth = 0;
    int in_string = 0;
    int escaped = 0;
    int started = 0;
    size_t index;
    for (index = 0; index < length; ++index) {
        uint8_t byte = buffer[index];
        if (in_string) {
            if (escaped)
                escaped = 0;
            else if (byte == '\\')
                escaped = 1;
            else if (byte == '"')
                in_string = 0;
            continue;
        }
        if (byte == '"') {
            in_string = 1;
        } else if (byte == '{' || byte == '[') {
            ++depth;
            started = 1;
        } else if (byte == '}' || byte == ']') {
            if (depth <= 0)
                return 0;
            --depth;
            if (depth == 0) {
                for (++index; index < length; ++index)
                    if (buffer[index] != ' ' && buffer[index] != '\t' &&
                        buffer[index] != '\r' && buffer[index] != '\n')
                        return 0;
                return 1;
            }
        }
    }
    return started && depth == 0 && !in_string;
}

void codex_reassembler_init(
    codex_reassembler_t *reassembler,
    uint8_t *buffer,
    size_t capacity
)
{
    if (reassembler == NULL)
        return;
    reassembler->buffer = buffer;
    reassembler->capacity = capacity;
    reassembler->length = 0;
}

void codex_reassembler_reset(codex_reassembler_t *reassembler)
{
    if (reassembler != NULL)
        reassembler->length = 0;
}

codex_wire_result_t codex_wire_encode_report(
    uint8_t channel,
    const uint8_t *payload,
    size_t payload_length,
    uint8_t report[CODEX_HID_REPORT_SIZE]
)
{
    if (report == NULL || (payload == NULL && payload_length != 0))
        return CODEX_WIRE_INVALID_ARGUMENT;
    if (!codex_wire_valid_channel(channel))
        return CODEX_WIRE_INVALID_CHANNEL;
    if (payload_length > CODEX_HID_PAYLOAD_SIZE)
        return CODEX_WIRE_OVERFLOW;

    memset(report, 0, CODEX_HID_REPORT_SIZE);
    report[0] = CODEX_HID_REPORT_ID;
    report[1] = channel;
    report[2] = (uint8_t)payload_length;
    if (payload_length != 0)
        memcpy(report + 3, payload, payload_length);
    return CODEX_WIRE_OK;
}

codex_wire_result_t codex_wire_decode_report(
    const uint8_t report[CODEX_HID_REPORT_SIZE],
    uint8_t *channel,
    const uint8_t **payload,
    size_t *payload_length
)
{
    if (report == NULL || channel == NULL || payload == NULL ||
        payload_length == NULL)
        return CODEX_WIRE_INVALID_ARGUMENT;
    if (report[0] != CODEX_HID_REPORT_ID ||
        report[2] > CODEX_HID_PAYLOAD_SIZE)
        return CODEX_WIRE_INVALID_REPORT;
    if (!codex_wire_valid_channel(report[1]))
        return CODEX_WIRE_INVALID_CHANNEL;

    *channel = report[1];
    *payload_length = report[2];
    *payload = report + 3;
    return CODEX_WIRE_OK;
}

codex_wire_result_t codex_reassembler_feed(
    codex_reassembler_t *reassembler,
    const uint8_t report[CODEX_HID_REPORT_SIZE],
    const uint8_t **message,
    size_t *message_length
)
{
    uint8_t channel = 0;
    const uint8_t *payload = NULL;
    size_t payload_length = 0;
    size_t newline_index = 0;
    codex_wire_result_t result;

    if (reassembler == NULL || reassembler->buffer == NULL ||
        message == NULL || message_length == NULL)
        return CODEX_WIRE_INVALID_ARGUMENT;

    *message = NULL;
    *message_length = 0;
    result = codex_wire_decode_report(
        report, &channel, &payload, &payload_length
    );
    if (result != CODEX_WIRE_OK)
        return result;
    if (channel != CODEX_HID_CHANNEL_RPC)
        return CODEX_WIRE_INVALID_CHANNEL;
    if (payload_length > reassembler->capacity - reassembler->length) {
        codex_reassembler_reset(reassembler);
        return CODEX_WIRE_OVERFLOW;
    }

    memcpy(
        reassembler->buffer + reassembler->length,
        payload,
        payload_length
    );
    reassembler->length += payload_length;

    for (newline_index = 0; newline_index < reassembler->length;
         ++newline_index) {
        if (reassembler->buffer[newline_index] == '\n')
            break;
    }
    if (newline_index == reassembler->length &&
        !codex_wire_json_object_complete(reassembler->buffer,
                                         reassembler->length))
        return CODEX_WIRE_OK;

    *message_length = newline_index == reassembler->length ?
                          reassembler->length : newline_index;
    if (*message_length != 0 &&
        reassembler->buffer[*message_length - 1] == '\r')
        --*message_length;
    *message = reassembler->buffer;

    /*
     * Current RPC traffic contains one JSON object per terminated sequence.
     * Reject rather than silently ignore data after the first LF.
     */
    if (newline_index != reassembler->length &&
        newline_index + 1 != reassembler->length) {
        codex_reassembler_reset(reassembler);
        *message = NULL;
        *message_length = 0;
        return CODEX_WIRE_INVALID_REPORT;
    }
    return CODEX_WIRE_MESSAGE_COMPLETE;
}
