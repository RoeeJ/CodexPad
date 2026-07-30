# duckyPad Pro as a Codex Micro-compatible controller

> This is the committed design and implementation plan. For the current
> accepted control layout, validated hardware state, and remaining acceptance
> work, use `docs/codex-goal.md` and `docs/implementation-status.md`.

## Executive decision

This project is feasible as a **wired USB, behaviorally compatible Codex controller** on the existing duckyPad Pro hardware.

The recommended approach is a new, opt-in firmware mode that preserves the normal duckyPad firmware and adds a Work Louder-compatible vendor HID control plane. The duckyPad cannot become a physical clone: it has 20 keys, two encoders, and an OLED, but no planar joystick, touch sensor, battery, or ambient ring. It can nevertheless provide the valuable Codex Micro behaviors:

- six keys that select Codex threads and display live agent state by RGB;
- configurable command keys for accept, reject, push-to-talk, new chat, and custom actions;
- an encoder for reasoning effort;
- a second encoder and spare keys as substitutes for joystick directions;
- an OLED summary of selected agent, state, connection, and reasoning level.

The main technical risk is not compute or peripherals. It is matching a private, versioned host-device protocol closely enough that current and future Codex desktop releases continue to recognize the device.

## Evidence and feasibility

### duckyPad Pro capabilities

The repository contains an ESP-IDF 5.3.1 application for an ESP32-S3. Its existing firmware already provides:

- a 4×5 matrix of 20 mechanical switches;
- 20 individually addressable RGB LEDs;
- two rotary encoders with push switches;
- a 128×128 OLED;
- USB keyboard, consumer-control, mouse, and a 64-byte bidirectional custom HID report;
- Bluetooth keyboard/mouse/media HID;
- SD-card profiles and firmware update support;
- event queues and FreeRTOS tasks that cleanly separate scanning, UI, LEDs, HID, and macro execution.

Relevant source locations:

- `firmware/dpp_fw/main/input_task.[ch]`
- `firmware/dpp_fw/main/neopixel_task.[ch]`
- `firmware/dpp_fw/main/hid_task.[ch]`
- `firmware/dpp_fw/main/ui_task.[ch]`
- `firmware/dpp_fw/main/main.c`

The current custom HID report is not directly compatible. It uses Generic Desktop usage `0x3A` and report IDs 4/5. Codex discovery requires a vendor HID collection on usage page `0xFF00`, and the Work Louder transport uses report ID 6.

### Current Codex/Work Louder contract

The assessment was made against Codex desktop build `26.721.41059` available locally on 2026-07-29. The current application:

- discovers Espressif/Work Louder VID `0x303A`;
- recognizes Codex Micro PID `0x8360`;
- also recognizes Creator Micro V2 PIDs `0x8297` and `0x8298`;
- filters for HID usage page `0xFF00`;
- opens the vendor HID endpoint with `node-hid`;
- fragments JSON text into 64-byte reports:
  - byte 0: report ID `6`;
  - byte 1: channel (`1` debug, `2` RPC);
  - byte 2: UTF-8 payload length, maximum `61`;
  - bytes 3–63: payload;
- reassembles device-to-host data until a CR/LF-terminated JSON object is complete.

Required host calls observed in the current integration:

| Method | Direction | Minimum implementation |
|---|---|---|
| `sys.version` | host → device | Return a firmware version result |
| `device.status` | host → device | Return a valid status; battery fields may be absent/null |
| `v.oai.rgbcfg` | host → device | Apply global key/ambient lighting or acknowledge unsupported ambient behavior |
| `v.oai.thstatus` | host → device | Apply six thread status entries to six mapped key LEDs |
| `v.oai.hid` | device → host | Notify key/encoder actions using `{k, act, ag}` |
| `v.oai.rad` | device → host | Optional joystick substitute using normalized `{a, d}` |

Agent keys must be emitted as `AG00` through `AG05`; Codex derives the slot from that naming convention.

The protocol packages embedded in the app are proprietary. Implementation must be clean-room and limited to independently authored interoperability code based on observed wire behavior. Do not copy bundled Work Louder source into this repository.

### Product behavior to match

The official setup guide defines six agent keys and these status colors:

- white: idle;
- blue: thinking;
- green: complete;
- amber: requires input;
- red: error;
- off: no assigned agent.

The product also supports command remapping inside Codex, reasoning control on the dial, joystick-triggered skills, Bluetooth/USB, and six Work Louder Input layers. Only the Codex integration is in scope for the first release; Work Louder Input compatibility is a separate project.

Sources:

- https://worklouder.cc/codex-micro
- https://worklouder.cc/openai-micro-setup
- https://openai.com/supply/co-lab/work-louder/

## Target architecture

### Mode boundary

Add a compile-time feature flag first:

```text
CONFIG_DPP_CODEX_MICRO_COMPAT
```

When enabled, boot into a dedicated Codex controller application path. Do not initially mix duckyScript execution and Codex RPC handling. Once the protocol is stable, add a persisted boot-mode selector:

- normal duckyPad mode;
- Codex compatibility mode;
- USB mass-storage mode.

This separation minimizes regressions and makes recovery straightforward.

### Modules

Add these firmware modules:

```text
main/
  codex_mode.c/.h          lifecycle, task creation, event routing
  codex_hid.c/.h           vendor HID descriptor and framed transport
  codex_rpc.c/.h           bounded JSON-RPC parser and response builder
  codex_lighting.c/.h      thread/global lighting model and LED mapping
  codex_controls.c/.h      switch/encoder → Codex notification mapping
  codex_ui.c/.h            OLED status view
```

Use ESP-IDF's bundled cJSON rather than introducing a new JSON dependency. Parse only the allowed methods and validate every type/range. Keep buffers bounded and reject oversized/incomplete messages.

### USB identity

Use VID `0x303A` and, for development, Creator Micro V2 PID `0x8297`, because current Codex explicitly supports that model. Do not claim the Codex Micro PID `0x8360` by default.

Add:

- manufacturer string: a truthful project-specific name, not “Work Louder”;
- product string: `duckyPad Pro Codex Controller`;
- a stable per-device serial derived from the ESP32 MAC;
- vendor HID usage page `0xFF00`;
- report ID 6 with 63-byte input and output payloads;
- the existing keyboard/consumer/mouse reports only if they can coexist without changing the vendor interface contract.

Gate: if Codex discovery rejects the truthful manufacturer despite VID/PID/usage matching, document this and decide explicitly whether a local Codex discovery patch is preferable to stronger device impersonation. The default recommendation is the local discovery patch.

### Control mapping

Use this initial layout, expressed in logical switch IDs so it can be revised without touching the transport:

```text
Row 1: AG00  AG01  AG02  AG03
Row 2: AG04  AG05  New   PTT
Row 3: Approve Reject Stop Review
Row 4: Custom1 Custom2 Custom3 Custom4
Row 5: Status  Dimmer Brighter Settings/Diagnostics

Upper encoder: reasoning effort down/up; press activates; hold opens Codex settings
Lower encoder: previous/next agent; press focuses selected agent; hold opens overview
```

Key down and key up should send distinct `v.oai.hid` notifications. Long press remains local unless Codex exposes a distinct action contract. The exact non-agent key identifiers must be captured from a real device, Codex configuration UI, or controlled host-side tests before freezing the mapping.

The local settings view covers LED brightness, OLED contrast and timeout,
auto-dim, boot mode, animation speed, restore defaults, firmware/about, and a
truthful post-v1 Bluetooth status. Diagnostics covers USB/RPC state, identity,
firmware/protocol versions, uptime, transport counters, current/minimum free
memory, last error, input and LED tests, and the recovery reminder.

### Lighting mapping

Maintain six `codex_thread_state` entries:

```c
typedef struct {
    uint8_t id;
    uint32_t rgb;
    float brightness;
    uint8_t effect;
    float speed;
    bool sync_keys;
    bool sync_ambient;
} codex_thread_state_t;
```

Map thread IDs 0–5 to the six agent-key LEDs. Implement:

- off;
- solid;
- breath;
- a lightweight “snake” approximation across the six agent LEDs;
- brightness clamping;
- selection indication without losing the underlying status;
- inactivity-off and restoration on local input.

Map the host's ambient zone to the remaining 14 key LEDs. The duckyPad has no physical ambient ring, so this is an intentional visual approximation.

The OLED should show:

- `CODEX` and connected/disconnected;
- selected slot and truncated thread title when available locally;
- status label/color name;
- current reasoning/action context;
- last protocol error in a diagnostic view.

## Work plan and exit criteria

### Phase 0 — Baseline and recovery

1. Record the exact PCB revision and confirm the checked-in pin map matches the physical unit.
2. Build unmodified firmware with the pinned ESP-IDF toolchain.
3. Flash and smoke-test keys, encoders, LEDs, OLED, SD, normal USB HID, and recovery/DFU.
4. Save the released firmware binary and SD-card contents.

Exit: stock firmware can be rebuilt, restored, and exercised with a written checklist.

### Phase 1 — Host protocol characterization

1. Add a repository-local `tools/codex_protocol_probe/` host utility.
2. Encode/decode Work Louder-style 64-byte frames.
3. Build golden packet fixtures for:
   - one-packet and multi-packet RPC calls;
   - split UTF-8 boundaries;
   - `sys.version`;
   - `device.status`;
   - `v.oai.rgbcfg`;
   - `v.oai.thstatus`;
   - `v.oai.hid`;
   - malformed length, JSON, method, and ID cases.
4. Determine exact JSON response result shapes and non-agent key identifiers using a real Creator/Codex Micro if available, or a mock HID endpoint plus current Codex logs.
5. Store findings in `docs/codex-protocol.md`, including the tested Codex build number.

Exit: every required message has an example request, response/notification, and documented semantics. Unknown command identifiers remain an explicit blocker to full command-key parity, not to agent-key MVP.

### Phase 2 — Vendor HID transport

1. Add the compile-time mode and USB descriptors.
2. Implement RX/TX frame assembly on channel 2.
3. Add bounded queues so TinyUSB callbacks do no parsing or LED work.
4. Add timeouts and reset behavior for truncated multi-report messages.
5. Add counters for RX, TX, malformed, overflow, and timeout events.
6. Verify enumeration on Linux, macOS, and Windows where available.

Exit: the host probe can exchange 1,000 sequential one- and multi-frame echo/test RPCs without loss, overflow, heap growth, or input starvation.

### Phase 3 — Minimum Codex handshake

1. Implement `sys.version`.
2. Implement `device.status` with truthful wired/no-battery values.
3. Implement JSON-RPC success and method/parameter error responses.
4. Verify Codex Settings shows the controller as connected.
5. Add a compatibility matrix keyed by Codex desktop version and OS.

Exit: current Codex connects for 30 minutes, periodically polls status, and shows no reconnect loop or transport error.

### Phase 4 — Agent lighting

1. Implement `v.oai.thstatus`.
2. Implement `v.oai.rgbcfg`.
3. Map six thread slots to LEDs and ambient state to spare LEDs.
4. Add OLED status rendering and selection overlay.
5. Test every state, brightness limit, effect, selection transition, inactivity-off, and reconnect restoration.

Exit: six simultaneous Codex threads display correct live states, transitions settle within 250 ms of host writes, and local input never corrupts the persistent host lighting model.

### Phase 5 — Inputs and actions

1. Emit `v.oai.hid` for `AG00`–`AG05` press/release.
2. Verify single-tap focus and double-tap foreground behavior.
3. Characterize and implement encoder and command identifiers.
4. Map the second encoder/four keys to `v.oai.rad` only if Codex's current joystick behavior requires it; otherwise prefer normal command notifications.
5. Test held keys, chatter, simultaneous keys, encoder bursts, disconnect during input, and reconnect.

Exit: agent selection, reasoning adjustment, PTT, new chat, accept, reject, and at least four configurable actions work from the duckyPad without ordinary keyboard-shortcut emulation.

### Phase 6 — Preserve duckyPad usability

1. Add persisted mode selection and a boot-time escape chord.
2. Preserve mass-storage/update/recovery paths.
3. Decide whether Codex mode reads mapping/brightness settings from SD or NVS.
4. Ensure normal mode retains duckyScript, profiles, Bluetooth HID, and current descriptors.
5. Add migration/versioning for new settings.

Exit: switching modes and restoring stock behavior is documented, deterministic, and does not require reflashing.

### Phase 7 — Bluetooth, post-v1

Bluetooth Codex control is explicitly deferred until after the wired v1 release. The existing BLE implementation exposes keyboard/mouse/media HID but not the Work Louder vendor channel. Stock-mode Bluetooth must remain intact. Codex mode must not expose a nonfunctional pairing flow; its settings UI reports `Bluetooth: unavailable in wired release`.

If pursued:

1. add a vendor HID over GATT report matching report ID 6;
2. advertise the compatible identity;
3. validate discovery through BlueZ, macOS, and Windows;
4. implement connection handoff and USB precedence;
5. test pairing channels, sleep/wake, and reconnection.

Exit: vendor RPC, lighting, and controls behave identically over BLE for a two-hour session.

### Phase 8 — Release engineering

1. Add unit tests for framing, parsing, lighting transforms, and mappings.
2. Add host integration tests that use hidraw/node-hid when hardware is present and skip clearly otherwise.
3. Add a hardware-in-loop checklist and a 4-hour soak test.
4. Add `docs/install.md`, `docs/recovery.md`, `docs/compatibility.md`, and a known-limitations section.
5. Produce a separately named firmware binary; never overwrite the stock artifact.
6. Review third-party notices, USB identity policy, and clean-room documentation.

Exit: a new user with a duckyPad Pro can back up, flash, connect, validate all supported controls, return to normal mode, and restore stock firmware using only repository instructions.

## Test matrix

At minimum:

| Area | Cases |
|---|---|
| Framing | lengths 0, 1, 60, 61, 62, 122, max; split JSON; CRLF/LF; bad channel/length |
| RPC | valid IDs 0–998; absent/wrong ID; unknown method; missing/wrong params; oversized JSON |
| Lighting | six states; six slots; 0/50/100% brightness; effect transitions; selection; inactivity |
| Input | press/release; long hold; double tap; simultaneous keys; fast encoder; queue overflow |
| Lifecycle | boot before Codex; Codex before boot; unplug during RPC; app restart; sleep/wake |
| Regression | normal duckyPad profiles, macros, OLED, RGB, USB HID, SD/MSC, DFU |
| Platforms | Linux current Codex first; macOS and Windows before release claim |

## Risks and mitigations

1. **Private protocol changes.** Pin a known-good Codex build in the compatibility matrix, isolate protocol constants, and keep golden captures. Fail visibly on unsupported traffic.
2. **USB VID/PID legitimacy.** A compatible identity is required for unmodified Codex discovery. Prefer a local host discovery extension for long-term distribution; use emulation only for personal interoperability testing.
3. **Proprietary code/license.** Reimplement the wire contract; do not import or redistribute bundled Work Louder packages.
4. **No joystick/touch/battery/ambient ring.** State these as hardware substitutions. Do not fabricate battery data.
5. **Firmware regression.** Keep Codex mode isolated until tested and preserve a boot escape plus stock image.
6. **Concurrent LED ownership.** In Codex mode, the Codex lighting model owns agent LEDs; local animations are transient overlays only.
7. **JSON/memory pressure.** Use fixed maximum message sizes, bounded queues, cJSON lifecycle tests, and fuzz malformed reports.
8. **Bluetooth scope creep.** Ship wired first.

## Definition of done

The project is complete when:

- a reproducible build produces a separately named Codex-compatible firmware;
- the current supported Codex desktop detects and remains connected to a wired duckyPad Pro;
- six physical keys select six threads and show correct live status colors;
- command keys and an encoder perform the agreed Codex-native actions;
- OLED feedback and error diagnostics work;
- no proprietary Work Louder source is committed;
- normal duckyPad mode and recovery remain functional;
- protocol, build, flash, validation, limitations, and restoration are documented;
- automated tests pass and the hardware completes the soak test;
- the compatibility matrix names the verified Codex build(s) and OS(es).

## Copy-ready Codex `/goal`

```text
Adapt the duckyPad Pro repository at /home/roee/Projects/CodexPad/duckyPad-Pro into a reversible, wired USB Codex Micro-compatible controller, following docs/implementation-plan.md as the governing plan and docs/codex-goal.md as the current executable handoff.

Do not stop at analysis or scaffolding. Continue until the implementation meets the plan's Definition of Done, or until a genuine hardware/external blocker is documented with reproducible evidence and all non-blocked work is complete.

Required outcomes:
1. Preserve stock duckyPad behavior and recovery. Build and validate the unmodified baseline first; never overwrite the released firmware artifact.
2. Add an opt-in Codex compatibility mode with an isolated Work Louder-compatible vendor HID interface: usage page 0xFF00, report ID 6, 64-byte reports with channel/length/61-byte payload framing, bounded reassembly, and newline-terminated device responses.
3. Implement clean-room JSON-RPC interoperability for sys.version, device.status, v.oai.rgbcfg, v.oai.thstatus, v.oai.hid, and any characterized control notifications needed by current Codex. Do not copy or commit proprietary Work Louder source.
4. Map six duckyPad keys and LEDs to Codex agent slots AG00–AG05; implement live idle/thinking/complete/requires-input/error/off lighting, selection overlays, brightness/effects, reconnect restoration, and OLED status.
5. Implement Codex-native agent selection, reasoning encoder control, push-to-talk, new chat, accept, reject, and at least four configurable actions. Treat exact non-agent identifiers as a characterization gate and record evidence.
6. Prefer wired USB for the completed release. Bluetooth is optional and must not delay wired Definition of Done.
7. Create protocol documentation, golden packet fixtures, host probe tooling, unit/integration tests, a hardware-in-loop checklist, build/flash/recovery docs, known limitations, and a Codex-version/OS compatibility matrix.
8. Use truthful product strings and document USB VID/PID interoperability and distribution constraints. Default development identity may use the currently recognized Creator Micro V2 PID 0x8297 with VID 0x303A; if truthful-manufacturer discovery fails, prefer a narrowly scoped local Codex discovery patch over stronger device impersonation.
9. Verify proportionally after every phase: compile, static checks, protocol tests, device enumeration, current Codex connection stability, all lighting/input behaviors, normal-mode regression, and a four-hour soak test when hardware is available.
10. Keep a running progress log in docs/implementation-status.md with completed acceptance criteria, commands/results, protocol observations, blockers, and next action. Update the plan only when evidence requires it, and explain any deviation.

Completion means the repository produces a separately named firmware that a user can flash to an existing duckyPad Pro, use with a supported current Codex desktop for six live agent slots and the agreed native controls, switch back to normal duckyPad mode, and recover to stock using repository documentation, with automated tests passing and hardware validation recorded.
```
