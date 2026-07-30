# Codex controller interoperability protocol

This document describes a clean-room interoperability target observed in Codex
desktop build `26.721.41059` on 2026-07-29. It is not a promise by OpenAI or
Work Louder and may change in later builds.

No proprietary Work Louder source may be copied into this project.

## Discovery

The current Codex desktop integration recognizes these USB identities:

| Device | VID | PID |
|---|---:|---:|
| Creator Micro V2 variant | `0x303A` | `0x8297` |
| Creator Micro V2 variant | `0x303A` | `0x8298` |
| Codex Micro | `0x303A` | `0x8360` |

Discovery additionally requires HID usage page `0xFF00`. Linux access policy in
the locally inspected Codex desktop package targets interface `00` for
`303a:8360`; a duckyPad development identity will need a matching local rule or
host discovery patch.

The firmware should use truthful strings:

- manufacturer: project/author-specific;
- product: `duckyPad Pro Codex Controller`;
- serial: stable and derived from the device MAC.

Development may use VID/PID `303A:8297` for personal interoperability testing.
Distribution under another vendor's allocated VID/PID requires an explicit
policy decision. A narrow host discovery patch is the preferred long-term path.

## HID framing

The vendor channel uses 64-byte HID reports and report ID 6:

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 1 | report ID, always `6` |
| 1 | 1 | channel |
| 2 | 1 | payload byte length, `0..61` |
| 3 | 61 | UTF-8 payload, zero padded |

Known channels:

- `1`: debug text;
- `2`: JSON-RPC.

A JSON message longer than 61 bytes is divided into sequential reports. There
is no observed packet sequence field; ordering is provided by HID delivery.
Current host requests do not include a delimiter; the device recognizes the
closing balanced top-level JSON object across reports. Device-to-host logical
messages must end in LF (CRLF is also accepted by the host parser). Length is a
byte count, not a Unicode character count.

Firmware requirements:

- reject unknown report IDs/channels;
- reject lengths above 61;
- maintain independent bounded reassembly buffers per channel;
- time out incomplete messages;
- never parse JSON in the TinyUSB callback;
- terminate each device response or notification with LF;
- preserve UTF-8 bytes across report boundaries.

## JSON-RPC shape

Host requests use:

```json
{"method":"sys.version","params":null,"id":123}
```

IDs observed by the host are integers in the range 0 through 998. Responses use
the same ID:

```json
{"result":{"version":"0.2.4"},"id":123}
```

The host also understands compact response/notification keys (`i`, `m`, `p`),
but the firmware should emit the readable full form.

Errors should be bounded JSON objects containing the request ID and an `error`
object with a numeric code and short message. Exact firmware error codes are
not yet characterized; use conventional JSON-RPC codes unless live testing
shows a stricter requirement.

## Required host-to-device methods

### `sys.version`

Return a semantic firmware/protocol version:

```json
{"result":{"version":"0.2.4"},"id":123}
```

The current host reads `response.result.version` directly.

### `device.status`

Return truthful device state:

```json
{
  "result": {
    "version": "0.2.4",
    "profile_index": 0,
    "layer_index": 0
  },
  "id": 123
}
```

The current host accepts optional `version` (string), `profile_index` (number),
`layer_index` (number), `battery` (number), and `is_charging` (boolean).
duckyPad Pro is wired and has no battery, so the two battery fields must be
omitted rather than fabricated.

The clean-room 0.2.4 firmware also returns a selected slot, USB state, and a
`diagnostics` object containing transport/input counters, RPC count, uptime,
current/minimum free memory, and the last bounded protocol error. These are
additive fields; the host's required fields remain unchanged.

Malformed JSON, invalid requests or IDs, wrong parameters, and unknown methods
return conventional bounded JSON-RPC errors (`-32700`, `-32600`, `-32602`, and
`-32601`). Accepted request IDs are the observed integer range `0..998`.

### `v.oai.rgbcfg`

Applies global key and ambient lighting:

```json
{
  "method": "v.oai.rgbcfg",
  "params": {
    "ambient": {"e": 0, "b": 1.0, "s": 0.0, "m": 0, "c": 16777215},
    "keys": {"e": 0, "b": 1.0, "s": 0.0, "m": 0, "c": 16777215}
  },
  "id": 1
}
```

Fields:

- `e`: effect enum;
- `b`: normalized brightness;
- `s`: normalized speed;
- `m`: effect-specific value;
- `c`: packed RGB integer `0xRRGGBB`.

The duckyPad maps ambient lighting to the 14 non-agent key LEDs.

### `v.oai.thstatus`

Applies six per-thread lighting records:

```json
{
  "method": "v.oai.thstatus",
  "params": [
    {"id": 0, "c": 16777215, "b": 1, "e": 0, "s": 0, "sk": 0, "sa": 0}
  ],
  "id": 2
}
```

Fields:

- `id`: thread slot `0..5`;
- `c`: packed RGB integer;
- `b`: normalized brightness;
- `e`: effect enum;
- `s`: normalized speed;
- `sk`: synchronize key lighting;
- `sa`: synchronize ambient lighting.

## Device-to-host notifications

Notifications have no ID.

### Agent and command controls

```json
{"method":"v.oai.hid","params":{"k":"AG00","act":1,"ag":0}}
```

- agent identifiers are `AG00` through `AG05`;
- `act` is `1` for press, `0` for release, and `2` for an encoder turn;
- `ag` is the optional agent index.

Codex derives the slot directly from the `AG0[0-5]` identifier.

Current Codex build `26.721.41059` recognizes action keys `ACT06`, `ACT07`,
`ACT08`, `ACT09`, `ACT10`, `ACT11`, and `ACT12`. Its settings combine
`ACT10`/`ACT11` into one microphone slot and expose the others as configurable
slots. The installed app's default keycaps are FAST, APPR, REJ, SPLIT, MIC, and
CODEX respectively; these are host-side defaults rather than firmware command
names.

The upper encoder sends `ENC_CW` or `ENC_CC` with `act:2`. `ENC_TO` press and
release is accepted by the current Codex encoder-press path. These identifiers
were independently characterized from the installed Codex app's public runtime
behavior on 2026-07-30; no Work Louder implementation is included here.

### Joystick/radial control

```json
{"method":"v.oai.rad","params":{"a":0.25,"d":1.0}}
```

- `a`: normalized angle `0..1`;
- `d`: normalized displacement `0..1`.

The duckyPad has no joystick. This notification may be synthesized from four
keys or the second encoder only after current Codex behavior is tested.

## Golden fixture policy

Fixtures under `tools/codex_protocol_probe/fixtures/` are independently
generated from the contract above. They are not captured proprietary source or
firmware blobs. Every change to framing or JSON shapes must update fixtures and
tests together.
