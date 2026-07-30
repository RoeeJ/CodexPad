# Compatibility matrix

Only directly tested combinations may be marked supported.

| OS | Codex desktop | Enumeration | RPC | Lighting | Inputs | Stability | Status |
|---|---|---|---|---|---|---|---|
| Linux | 26.721.41059 | Pass | Pass | RPC pass; visual confirmation pending | Wire events pass; app actions pending | 1,000 RPC pass; 4h pending | Validation in progress |
| macOS | — | — | — | — | — | — | Not tested |
| Windows | — | — | — | — | — | — | Not tested |

## Static interoperability evidence

Codex desktop build `26.721.41059` recognizes:

- `303a:8360` as Codex Micro;
- `303a:8297` and `303a:8298` as Creator Micro V2;
- HID usage page `0xff00`;
- report ID 6 and the framed channel-2 JSON-RPC protocol documented in
  `codex-protocol.md`.

Live Linux validation on serial `DP24_BD761844` proves truthful enumeration,
descriptor shape, all required RPC acknowledgements, every characterized input
notification, persisted stock/Codex switching, reconnect recovery, and 1,000
error-free exchanges. Direct Codex UI/action confirmation and the four-hour
soak remain before this combination is marked fully supported.
