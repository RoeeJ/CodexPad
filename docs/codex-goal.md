# Codex `/goal`: finish the duckyPad Pro Codex controller

Complete the wired duckyPad Pro to Codex Micro-compatible controller in
`/home/roee/Projects/CodexPad/duckyPad-Pro`, continuing from the installed
0.2.4 baseline and its proven live RPC health. Do not restart with feasibility
analysis and do not stop at scaffolding. Preserve the released 3.3.5 recovery
image, the device SD backup outside Git, the stock firmware path, and the
truthful development USB identity. Keep `docs/implementation-status.md`
current and distinguish proven hardware behavior from unverified claims. The
0.2.4 OLED polling fix still requires explicit visual confirmation.

The current accepted control layout is:

- R1B1–R2B2: `AG00`–`AG05`;
- R2B3–R3B2: `ACT06`–`ACT09`;
- R3B3: microphone via `ACT10`;
- R3B4: the separate bottom-right Codex control via `ACT12`;
- R4B1–R4B4: aliases of `ACT06`–`ACT09`;
- R5B1–R5B4: local status, dimmer, brighter, and settings/diagnostics;
- upper encoder: `ENC_CC`/`ENC_CW`/`ENC_TO`, hold for Settings;
- lower encoder: agent select/focus, hold for Overview.

Treat `firmware/DPP_FW_CODEX_0.2.4_f5e25d6a.bin` as the current updater
artifact. Its SHA-256 is
`efafedfce90733ea75d706c1880a4bf6c81ca245e35ef2cf7a184f0dbc6d0632`.
Retain earlier artifacts only as exact hardware-test provenance.

Finish the remaining acceptance work:

1. Confirm Codex's 80% brightness remains after returning from the one-boot
   stock escape, while stock settings remain isolated.
2. Exercise every Settings, Diagnostics, Overview, and About page, including
   the LED/input tests, OLED auto-dim/off, lighting inactivity-off, and
   restoration without losing the host lighting model.
3. Complete the stock-mode keys, encoders, profiles, OLED, LEDs, SD, USB HID,
   mass-storage, and Bluetooth regression checklist.
4. Restore the retained released 3.3.5 image through USB storage, verify its
   stock identity and core peripherals, then reinstall 0.2.4 and repeat the
   version/status/control smoke checks.
5. Re-run the Python and strict native C suites, both final ESP-IDF builds,
   strict live JSON-RPC errors, 1,000 exchanges, simultaneous input/RPC
   traffic, reconnect tests, and diagnostic-counter/heap checks after any
   firmware change.
6. Validate current Codex on Linux for a 30-minute connection session. Record
   macOS/Windows as unverified unless those systems are actually available;
   do not infer cross-platform compatibility.
7. Only after the focused checks pass, run the four-hour wired Codex soak.
   Require stable heap, zero unexpected transport/input counter growth, no
   reconnect loop, responsive controls, and correct lighting/OLED behavior.

Fix defects found during validation, rebuild under ESP-IDF 5.3.1, create a new
versioned CRC-named artifact, reflash, and repeat the affected regression.
Completion requires an executable installation/recovery guide, current
compatibility and limitation documentation, preserved recovery assets, passing
automated tests/builds, and recorded physical evidence for every supported
claim. Do not mark the goal complete while required physical validation,
released-image restore, or the final soak remains unfinished.
