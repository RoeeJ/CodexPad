# Hardware-in-loop validation checklist

Record date, operator, PCB revision, serial, firmware SHA-256, Codex build, OS,
and failures in `implementation-status.md`.

## Before changing firmware

- [ ] Confirm serial and USB identity.
- [ ] Photograph or record PCB revision.
- [ ] Back up SD-card contents.
- [ ] Verify all 20 keys.
- [ ] Verify both encoders and encoder switches.
- [ ] Verify all 20 RGB LEDs and OLED.
- [ ] Verify normal HID, SD profiles, and USB-storage mode.
- [ ] Enter ROM download mode and exit without writing.

## Codex enumeration and transport

- [ ] Enumerates as `303a:8297` with truthful strings.
- [ ] Exposes usage page `0xff00`, report ID 6, 63-byte IN/OUT reports.
- [ ] Probe completes `sys.version` and `device.status`.
- [ ] `probe verify --count 1000` completes with zero failures.
- [ ] Probe completes one-frame, multi-frame, UTF-8, and maximum-size cases.
- [ ] Malformed length/channel/JSON traffic is rejected without reset.
- [ ] Malformed JSON, invalid IDs/params, and unknown methods return bounded
      JSON-RPC errors with the correct request ID/null-ID behavior.
- [ ] Simultaneous RPC and rapid control input never interleave logical HID
      messages.
- [ ] 1,000 sequential RPC exchanges complete without loss.
- [ ] Unplug during partial RPC and reconnect recovers cleanly.

## Codex behavior

- [ ] Current Codex reports the controller connected.
- [ ] `AG00` through `AG05` press and release select the correct slots.
- [ ] Six idle/thinking/complete/input/error/off states match Codex.
- [ ] Brightness and every supported effect behave correctly.
- [ ] Selection and inactivity overlays restore underlying state.
- [ ] Reasoning and all seven characterized `ACT06`–`ACT12` inputs work
      natively with their current host assignments.
- [ ] R3B3 emits the microphone action, R3B4 emits `ACT12`, row 4 mirrors
      `ACT06`–`ACT09`, and keys 16–19 remain local.
- [ ] OLED connection, selection, state, action, and error views are correct.
- [ ] Settings persist across reboot: LED brightness/idle-off, OLED
      contrast/timeout, auto-dim, boot mode, and animation speed.
- [ ] Settings restore-defaults and about/Bluetooth status are truthful.
- [ ] All diagnostics pages, input test, LED test, and recovery reminder work.
- [ ] OLED auto-dim/off and LED inactivity-off restore immediately on input
      without losing host lighting state.
- [ ] Thirty-minute connection test has no reconnect loop.

## Regression and recovery

- [ ] `+` persists stock mode, `-` persists Codex mode, and both side buttons
      enter stock for one boot without changing the persisted choice.
- [ ] Stock path enumerates as `0483:d11d` with a 205-byte descriptor.
- [ ] Repeat the complete stock peripheral and profile checklist.
- [ ] Restore released 3.3.5 through USB storage.
- [ ] Restore a development build through ROM download mode.
- [ ] Four-hour Codex soak completes with stable heap and error counters.
