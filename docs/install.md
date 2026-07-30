# Build and install the Codex controller firmware

This firmware is experimental. Read `recovery.md` first and retain the released
stock image before changing a device.

## Supported build

- ESP-IDF: 5.3.1
- target: ESP32-S3
- dependencies: pinned by `dependencies.lock`
- Codex feature: `CONFIG_DPP_CODEX_MICRO_COMPAT=y`

Build stock mode:

```sh
cd firmware/dpp_fw
idf.py build
```

Build Codex mode without modifying the repository `sdkconfig`:

```sh
cp sdkconfig /tmp/dpp-codex-sdkconfig
# Replace the disabled DPP_CODEX_MICRO_COMPAT line with:
# CONFIG_DPP_CODEX_MICRO_COMPAT=y
# CONFIG_DPP_CODEX_MAX_MESSAGE_SIZE=4096
idf.py -B /tmp/dpp-codex-build \
  -D SDKCONFIG=/tmp/dpp-codex-sdkconfig build
```

The checked-in development artifact is
`firmware/DPP_FW_CODEX_0.2.4_f5e25d6a.bin`. Verify it before use:

```text
size: 888672 bytes
CRC-32: f5e25d6a
SHA-256: efafedfce90733ea75d706c1880a4bf6c81ca245e35ef2cf7a184f0dbc6d0632
```

## Drag-and-drop installation

1. In stock firmware, long-press either `+` or `-`.
2. Select **Mount USB**.
3. Copy `DPP_FW_CODEX_0.2.4_f5e25d6a.bin` to the mounted duckyPad drive without
   renaming it.
4. Safely eject the drive.
5. Long-press `+` or `-` again to reboot.
6. Confirm the CRC screen, then press a key to install.

Do not unplug the device during the update.

## Expected Codex-mode identity

```text
VID:PID       303a:8297 (development interoperability identity)
manufacturer  CodexPad clean-room project
product       duckyPad Pro Codex Controller
usage page    0xff00
report ID     6
```

This VID/PID is suitable only for personal interoperability testing. It is not
a distributable USB identity owned by this project.

## Linux hidraw access

Codex desktop `26.721.41059` recognizes PID `8297`, but its packaged udev rule
grants interface-00 access only to production PID `8360`. Install this
project's narrowly scoped development rule:

```sh
sudo install -m 0644 packaging/70-codexpad-codex.rules \
  /etc/udev/rules.d/70-codexpad-codex.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw
```

Reconnect the controller if the existing hidraw node does not acquire an ACL.
The rule matches only `303a:8297`, USB interface `00`; it does not open generic
HID devices.

## Selecting the persisted boot mode

Hold `+` alone for at least 800 ms during boot to select and remember the
normal duckyPad path. Hold `-` alone to select and remember Codex mode. Holding
both is the non-persistent recovery escape into the stock path. Stock mode
uses the original `0483:d11d` identity, SD profiles, duckyScript, Bluetooth
policy, and USB-storage updater.

See `controls.md` for the Codex input layout and live probe commands.

Version 0.2.4 adds persistent local settings, structured OLED
settings/diagnostics/overview screens, selected-agent and inactivity lighting,
strict JSON-RPC errors and diagnostics, serialized HID transmission, and all
20 matrix keys. Encoder tap actions are uniformly deferred until release so
long holds cannot activate a setting or leak a host action. R3B3 maps to the
combined microphone slot, R3B4 maps to Codex's separate bottom-right control,
and row 4 mirrors the four preceding non-microphone actions. Earlier artifacts
are retained only for exact hardware provenance. Repeated passive thread-status
polls update state/lighting without replacing the OLED's local action context;
use 0.2.4.
