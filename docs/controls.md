# Codex-mode controls

The initial layout uses native Codex Micro notifications, not ordinary
keyboard shortcuts.

| duckyPad input | Notification | Codex behavior |
|---|---|---|
| keys 0–5 | `AG00`–`AG05`, press/release | select the six agent slots |
| keys 6–9 | `ACT06`–`ACT09`, press/release | four independently configurable Codex action slots |
| key 10 (row 3, button 3) | `ACT10`, press/release | Codex's combined microphone control |
| key 11 (row 3, button 4) | `ACT12`, press/release | the separate bottom-right Codex control |
| keys 12–15 (row 4) | aliases for `ACT06`–`ACT09`, press/release | duplicates of R2B3, R2B4, R3B1, and R3B2 |
| key 16 | local only | return to the live status screen |
| key 17 | local only | decrease LED brightness or the selected setting |
| key 18 | local only | increase LED brightness or the selected setting |
| key 19 | local only | settings; hold for diagnostics |
| upper encoder turn | `ENC_CW` / `ENC_CC`, `act:2` | reasoning effort when encoder mode is Reasoning |
| upper encoder tap | `ENC_TO`, press/release | open/activate the current encoder target |
| upper encoder hold | local only | open local settings |
| lower encoder turn | synthesized agent tap | previous/next agent |
| lower encoder tap | selected `AG0x`, press/release | focus selected agent |
| lower encoder hold | local only | open the six-agent overview |

Encoder-switch tap actions execute on release. If the press becomes a hold,
the release is consumed locally; a hold therefore cannot activate the selected
setting or leak a host tap while changing screens.

Codex desktop `26.721.41059` groups `ACT10` and `ACT11` into one microphone
slot. Its other action slots can be assigned to New chat, Approve, Reject,
Stop, Review, shortcuts, skills, or other actions in Codex Micro settings.
The app's defaults are host configuration and can change without reflashing.

All 20 matrix keys now have a defined role. Codex build `26.721.41059` exposes
only seven recognized wire identifiers for actions, with `ACT10`/`ACT11`
forming one microphone slot. R3B3 uses `ACT10`; assigning a separate duckyPad
key to `ACT11` would duplicate that same microphone action. R3B4 therefore
sends `ACT12`, matching the separate bottom-right control in Codex, and row 4
mirrors the four preceding non-microphone actions. The aliases are convenient
duplicates, not four additional independent host slots. This is an observed
protocol limit and is kept explicit.

The local settings screen covers LED brightness and idle-off, OLED contrast
and timeout, auto-dim, persisted boot mode, animation speed, restore defaults,
and firmware/about. The about view truthfully reports Bluetooth as unavailable
in the wired release. Turn the lower encoder to choose an entry; turn the upper
encoder or use keys 17/18 to change it; tap the upper encoder to activate
restore/about; tap the lower encoder to close.

Diagnostics pages show USB/RPC and identity, transport/input counters, uptime
and current/minimum heap, the last protocol error, the recovery reminder, a
live LED test, and a live input test. Turn the lower encoder between pages and
tap the upper encoder on the LED-test page to start or stop it. Local settings
are versioned in NVS and preserve the legacy stock/Codex boot selector.

Hold `+` or `-` at boot to select the persisted stock/Codex mode as described
in `recovery.md`.

## Interactive validation

With the Python `hid` package installed:

```sh
python3 -m tools.codex_protocol_probe.probe listen --seconds 90
```

Press every mapped key and rotate/click both encoders. The output must contain
matching press/release pairs and no dropped events. Then run:

```sh
python3 -m tools.codex_protocol_probe.probe verify --count 1000
```
