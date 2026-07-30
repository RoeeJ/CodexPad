# Codex controller implementation status

Last updated: 2026-07-30

## Goal

Adapt duckyPad Pro into a reversible wired Codex-compatible controller while
preserving stock behavior and recovery. The repository sources of truth are
`docs/implementation-plan.md` and the executable long-horizon handoff in
`docs/codex-goal.md`.

## Baseline evidence

- Repository commit: `0414e1661db5f3101bec3858c51874ac1ea01cfd`
- Working tree was clean before implementation began.
- Released recovery image:
  `firmware/DPP_FW_3.3.5_7e717835.bin`
- Released image size: `864560` bytes
- Released image SHA-256:
  `e3266c0fde56d6103f612c5b36eb24ed09e448d413758615cf1f28a1c4ce01a4`
- The stock recovery procedure exists in `doc/fw_update.md`; Codex-specific
  installation and recovery guidance is committed in `docs/install.md` and
  `docs/recovery.md`, with released-image restoration still awaiting hardware
  validation.
- A physical stock device was detected on 2026-07-29 as `0483:d11d`, serial
  `DP24_BD761844`, manufacturer `dekuNukem`, product `duckyPad Pro`.
- Its live sysfs HID report descriptor is 205 bytes and byte-for-byte matches
  the source descriptor's report IDs 1 through 5. It exposes one HID interface,
  one 16-byte interrupt-IN endpoint, USB 2.00 full-speed, and 100 mA bus power.

### Baseline build

Status: **attempted; repository baseline currently fails to link**.

ESP-IDF 5.3.1 and the ESP32-S3 compiler were installed for this run. An
unmodified snapshot from `git archive HEAD` was built under `/tmp`, separately
from the working tree. The checked-in `managed_components` cannot be consumed
by current IDF Component Manager because their integrity metadata is absent.
Resolving the declared versions afresh selected transitive
`espressif/tinyusb 0.21.0~1` and compiled the full project, but linking failed:

```text
multiple definition of `tud_umount_cb'
.../espressif__esp_tinyusb/tusb_msc_storage.c:629: first definition
.../main/hid_task.c:188: second definition
```

Disabling Component Manager and supplying the checked-in directories as extra
components also fails during configuration because
`espressif__esp_tinyusb/CMakeLists.txt` addresses a TinyUSB target that the
build has not created at that point.

This proves the toolchain reaches compilation and exposes a pre-existing
baseline dependency/reproducibility defect. It does not prove a successful
stock source build.

Resolution in the adaptation branch:

1. replaced the application's second global TinyUSB unmount callback with
   `tud_mounted()`, which is TinyUSB's authoritative connection state;
2. preserved Espressif's callback, which is required for SD-card ownership
   transitions in MSC mode;
3. built stock mode successfully against freshly resolved components;
4. built Codex mode successfully from the same source with the opt-in Kconfig
   flag.

After pinning TinyUSB, the current successful stock-mode image is `866624`
bytes with SHA-256
`9c937e62e751dd1ea5f01d5f5d5ffb9780ada0c33e86017507ebae96483f366d`.
The matching dual-path Codex-mode 0.2.4 image is `888672` bytes with SHA-256
`efafedfce90733ea75d706c1880a4bf6c81ca245e35ef2cf7a184f0dbc6d0632`.
It includes first-boot OTA validation plus a boot escape into the complete
stock application. Its updater-compatible, separately named artifact is
`firmware/DPP_FW_CODEX_0.2.4_f5e25d6a.bin`; `f5e25d6a` is the required filename
CRC-32.
The build results alone are not hardware validation. The separate 0.2.4
installation and live RPC evidence is recorded below; explicit visual
confirmation of its OLED polling fix remains pending. Component-manager
reproducibility was repaired by upgrading the lock file to format 2, directly
pinning `espressif/tinyusb 0.15.0~10`, and adding the registry integrity hashes
that were absent from the checked-in managed components.

The earlier `DPP_FW_CODEX_0.1.0_191cc173.bin` and
`DPP_FW_CODEX_0.2.0_6a850a45.bin` remain because they are the exact images used
for the live characterizations below. The current 0.2.4 image adds the
tap-on-release correction found during 0.2.0 validation and places the
microphone/bottom-right controls at R3B3/R3B4, with row 4 mirroring
`ACT06`–`ACT09`. The intermediate 0.2.1 build was never flashed and is retained
outside Git in `../local-artifacts/`; 0.2.2 was flashed only briefly before the
operator corrected the intended coordinate.
0.2.4 additionally prevents passive six-slot host polling from replacing the
OLED context line with the last record (`Agent 6 status updated`).

## 0.2.x implementation slice

- Split the runtime into explicit `codex_hid`, `codex_rpc`,
  `codex_lighting`, `codex_controls`, `codex_settings`, `codex_ui`, and
  `codex_mode` modules.
- Added strict, bounded JSON-RPC parse/request/params/method errors, ID range
  validation, additive runtime diagnostics, and atomic lighting validation.
- Serialized each full outbound JSON message plus LF so an RPC response cannot
  interleave with a simultaneous control notification.
- Added versioned NVS settings with legacy boot-selector migration and no NVS
  erase path: LED brightness/idle-off, OLED contrast/timeout/auto-dim,
  animation speed, boot mode, and restore defaults.
- Added structured home, six-agent overview, settings, about, and seven-page
  diagnostics UI, including live input/LED tests and truthful wired-only
  Bluetooth status.
- Added selected-agent lighting without replacing host state, master
  brightness/speed, inactivity-off/restoration, a larger Codex input queue,
  and an input-drop counter.
- Defined all 20 matrix keys without inventing unsupported host identifiers;
  key 10 is microphone, key 11 is `ACT12`, keys 12–15 are documented aliases,
  and keys 16–19 are local controls.
- In 0.2.1, made every encoder-switch tap execute on release. Long holds consume
  that release, preventing accidental setting activation or host-event leakage
  across a local page transition.
- In 0.2.3, assigned R3B3 to the combined microphone, R3B4 to `ACT12`, and
  row 4 to physical aliases of `ACT06`–`ACT09`. This avoids spending two
  duckyPad keys on Codex's combined `ACT10`/`ACT11` microphone slot.
- In 0.2.4, stopped passive `v.oai.thstatus` polling from overwriting the OLED
  local-action context. Slot state and lighting still update and redraw.

## Protocol observations

Current evidence is documented in `docs/codex-protocol.md`. No proprietary Work
Louder source is or will be committed. Repository code and fixtures are
independent implementations of the observed wire contract.

On 2026-07-30, static behavioral inspection of installed Codex desktop build
`26.721.41059` established the exact non-agent identifiers:
`ACT06`–`ACT12`, `ENC_CW`, `ENC_CC`, and `ENC_TO`. The same build maps
`ACT10`/`ACT11` to a combined microphone slot and exposes six host-configurable
action slots. The 0.1.0 hardware run subsequently captured all of those
identifiers. This resolves the earlier identifier gate. The 0.2.0 control
policy was exercised live; its tap-on-release and corrected physical action
layout were proven on 0.2.3. The installed 0.2.4 runtime has healthy live RPC;
its OLED polling correction remains visually unconfirmed.

## Acceptance criteria progress

| Criterion | Status | Evidence |
|---|---|---|
| Released recovery image preserved | Proven | Path, size, and SHA-256 above |
| Unmodified source builds | Contradicted | Untouched baseline has duplicate `tud_umount_cb` |
| Adaptation stock mode builds | Proven locally | ESP-IDF 5.3.1, pinned dependencies, 866624-byte image |
| Adaptation Codex mode builds | Proven locally; 0.2.4 installed | ESP-IDF 5.3.1, pinned dependencies, 888672-byte 0.2.4 image and SHA-256 above; live version/status below |
| Stock USB enumeration | Proven on hardware | `0483:d11d`, correct strings, 205-byte descriptor |
| Stock input/OLED/LED/MSC smoke test | Partial on hardware | Operator confirmed normal OLED/LED behavior and MSC; complete stock input/profile checklist remains |
| Golden framing and RPC tests | Proven locally | 11 Python tests plus strict native C transport/control/settings/lighting/RPC suites pass |
| Vendor HID transport | 0.2.4 live health proven; load regression on 0.2.3 | Truthful identity and healthy 0.2.4 version/status; 238-byte descriptor and 1,000 exchanges proven on 0.2.3 |
| JSON-RPC firmware methods | Proven on 0.2.0 hardware | Four methods plus live `-32700`/`-32600`/`-32601`/`-32602` errors and diagnostics |
| Six agent keys and lighting | Core and effects proven on 0.2.0; inactivity pending | Six agent edges and lighting RPCs live; operator confirmed all seven effects were visibly distinct |
| Native and local controls | Proven on 0.2.3 | Corrected R3/R4 layout paired exactly; closing Settings emitted no leaked host event; encoder turn/tap/hold coverage retained from 0.2.0 |
| Structured OLED/settings/diagnostics | Core screens and persistence proven; 0.2.4 OLED fix visually pending | Operator confirmed legible Settings/Overview and 80% LED brightness persisted across power cycle on 0.2.3; about/input/LED-test tour and explicit OLED polling-fix confirmation remain |
| Reversible normal/Codex modes | One-boot escape proven on 0.2.3 | Both buttons produced stock `0483:d11d` for one boot, then a normal reboot returned automatically to healthy 0.2.3 |
| Documentation and compatibility matrix | Complete off-device | Protocol, controls, install, recovery, HIL, and compatibility docs |

## Commands and results

```text
sha256sum firmware/DPP_FW_3.3.5_7e717835.bin
e3266c0fde56d6103f612c5b36eb24ed09e448d413758615cf1f28a1c4ce01a4

stat -c '%n %s bytes' firmware/DPP_FW_3.3.5_7e717835.bin
firmware/DPP_FW_3.3.5_7e717835.bin 864560 bytes

git rev-parse HEAD
0414e1661db5f3101bec3858c51874ac1ea01cfd

./tools/codex_protocol_probe/run_native_tests.sh
native Codex controller tests passed

python3 -m unittest discover -s tools/codex_protocol_probe -p 'test_*.py' -v
Ran 11 tests ... OK

idf.py -B /tmp/codexpad-stock-pinned ... build
866624-byte image; exit 0

idf.py -B /tmp/codexpad-codex-pinned ... build
888672-byte 0.2.4 image; exit 0
```

## Known limitations

- 0.2.4 is installed and live `sys.version`/`device.status` health is proven;
  the corrected action layout, tap-on-release behavior, 1,000-exchange
  transport regression, settings persistence, and one-boot stock escape were
  proven on 0.2.3;
- the 0.2.4 OLED polling fix still needs explicit visual confirmation;
- inactivity, full stock peripherals, and released-image restore remain;
- lighting implements off, solid, snake, rainbow, breath, gradient, and shallow
  breath with normalized brightness/speed; exact visual parity is unverified;
- the host does not transmit semantic thread titles or reasoning labels over
  the characterized RPC surface, so the OLED truthfully shows slot, color/
  effect state, and local action context instead;
- current Codex exposes seven action identifiers/six configurable slots, so
  keys 12–15 are physical aliases rather than additional independent actions;
- the compatibility VID/PID is development-only and must not be distributed
  under another vendor's allocation;
- macOS/Windows compatibility, released-image recovery restore, visible effect
  parity, complete stock peripheral regression, and the final soak remain
  unverified.

## Next action

Explicitly confirm that normal host polling no longer replaces the OLED context
line with `Agent 6 status updated`. Then complete the diagnostics/about tour,
persistence/inactivity checks, full stock peripherals, and released-image
recovery regression before starting the four-hour soak. Re-run the focused
R3/R4 and encoder-transition checks after any firmware change.

## Current external step

Operator interaction is required for the remaining visual settings,
persistence/inactivity, and stock/released-image recovery checks. No firmware
installation blocker remains.

## Hardware validation evidence — 0.2.4 on 2026-07-30

- Installed `DPP_FW_CODEX_0.2.4_f5e25d6a.bin` through the device USB-storage
  updater and re-enumerated in Codex mode.
- Live `sys.version` reports 0.2.4 and `device.status` responds successfully,
  proving installation and basic RPC health.
- At checkpoint validation, `device.status` reported USB connected with zero
  dropped, rejected, timed-out, failed-transmit, and dropped-input counters;
  free/minimum heap was 159248/156056 bytes.
- Two deliberately out-of-range request IDs returned the expected `-32600`
  error before the valid version/status calls, so `last_error` truthfully
  records `RPC invalid request`.
- The source/build prevents passive six-slot `v.oai.thstatus` polling from
  replacing the OLED local-action context. Explicit visual confirmation on the
  physical OLED remains pending and is not claimed by this checkpoint.

## Hardware validation evidence — 0.2.3 on 2026-07-30

- Copied `DPP_FW_CODEX_0.2.3_b2dadf74.bin` to USB-storage serial
  `DP24_BD761844`, flushed it, read it back byte-for-byte with matching
  SHA-256
  `71600fc7252db415587785de6c2b1095f4686d02f8657f52792ef3b1bc5510cb`,
  safely unmounted, and completed the device updater.
- Re-enumerated as truthful `303a:8297`; live `sys.version` and
  `device.status` report 0.2.3 with stable serial and clean initial counters.
- A focused physical capture produced, in order, R3B3 `ACT10`, R3B4 `ACT12`,
  and row-four `ACT06`, `ACT07`, `ACT08`, `ACT09`, each with paired
  press/release edges. This matches the corrected requested layout exactly.
- Holding upper to enter Settings and tapping lower to close it emitted no
  `AG0x`, `ENC_TO`, or other control notification, validating the
  tap-on-release page-transition fix on the wire.
- Changed local LED brightness to 80%, allowed the deferred NVS save, power
  cycled the controller, and confirmed the Settings screen still reported 80%.
  The device returned as 0.2.3 with all diagnostic error counters zero,
  proving versioned settings load/save across a real power cycle.
- Reconnected while holding both side buttons and observed the complete stock
  USB shape (`0483:d11d`, three interfaces) for one boot. A subsequent normal
  reconnect returned automatically to the single-interface `303a:8297` Codex
  runtime, still reporting 0.2.3 with every diagnostic error counter zero.
  This proves the recovery escape does not overwrite the persisted Codex mode.
- The operator confirmed the stock OLED and LEDs looked normal. Stock
  brightness did not inherit the Codex 80% setting, which is intentional:
  Codex settings are isolated in the `codexpad` NVS namespace while stock
  retains its existing settings/configuration path.
- A 1,000-exchange regression completed in 13.43 seconds with zero failures.
  Final diagnostics showed zero dropped/rejected/timed-out receives, failed
  transmissions, or dropped inputs; free/minimum heap remained
  159264/156056 bytes and `last_error` remained empty.

## Hardware validation evidence — 0.2.2 on 2026-07-30

- Copied `DPP_FW_CODEX_0.2.2_cc261dbd.bin` to the exact USB-storage volume,
  flushed it, read it back byte-for-byte with matching SHA-256
  `367dc974b936e99db11b4c463be5a1c6a46e583d3622d079932e6b539ea533e4`,
  and safely unmounted the volume.
- Re-enumerated as truthful `303a:8297`; live `sys.version` and
  `device.status` reported 0.2.2 with zero dropped/rejected/timed-out frames,
  failed transmissions, or dropped inputs.
- A 100-exchange post-update smoke test completed in 1.30 seconds with zero
  failures. The operator then corrected the requested physical coordinate
  before action-layout acceptance, so 0.2.2 is superseded by 0.2.3.

## Hardware validation evidence — 0.2.0 on 2026-07-30

- Copied `DPP_FW_CODEX_0.2.0_6a850a45.bin` to the exact USB-storage volume
  for serial `DP24_BD761844`, flushed it, and read it back with matching
  SHA-256
  `c1eb10199ae33556cf8508cdc8ed1bb81edcc2c54270e8804adc804985d39fcc`.
- Re-enumerated as truthful `303a:8297` with the expected 238-byte report
  descriptor and stable serial. Live `sys.version` reports `0.2.0`.
- Live `device.status` exposed USB/RPC state, selected slot, all transport/input
  counters, uptime, current/minimum heap, and the last error. Initial heap was
  159264 bytes with a 156056-byte minimum.
- Live invalid-ID, wrong-params, unknown-method, and balanced malformed-JSON
  requests returned `-32600`, `-32602`, `-32601`, and `-32700` respectively.
- A deliberately incomplete `{` request timed out and incremented
  `rx_timeouts` exactly once; the subsequent balanced malformed request set
  `last_error` to `RPC parse error` without resetting the device.
- A 100-exchange smoke run completed in 1.74 seconds, then a 1,000-exchange run
  completed in 17.85 seconds with zero failures. `rx_dropped`, `tx_failed`,
  and `input_dropped` remained zero.
- A valid `ENC_CW` notification arrived asynchronously during an RPC stability
  run. It was complete and uncorrupted, demonstrating message serialization;
  the probe was corrected to demultiplex notifications from response IDs, and
  a regression test was added.
- Captured paired edges for keys 0–15:
  `AG00`–`AG05`, `ACT06`–`ACT12`, then the documented row-four
  `ACT06`–`ACT08` aliases. The four row-five presses emitted no protocol
  notifications as designed. Upper encoder tap emitted one paired `ENC_TO`.
- A focused 0.2.0 capture received `ENC_CC` and `ENC_CW` for the upper encoder,
  and a lower-encoder tap emitted a paired `AG00` notification. The operator
  confirmed that upper hold opened Settings and lower hold opened Overview,
  with no deferred tap leaking from either hold.
- Applied all seven lighting renderers live (`off`, `solid`, `snake`,
  `rainbow`, `breath`, `gradient`, and `shallow-breath`); every RPC returned
  `true`, the operator confirmed visibly distinct stages, and the controller
  was restored to steady blue. Transport/input failure counters remained zero.

Still pending for 0.2.0: the complete diagnostics/about page tour, settings
persistence and inactivity restoration, stock-mode/recovery regression,
released-image restore, and the final soak.

## Prior hardware validation evidence — 0.1.0 on 2026-07-30

- Backed up 122 SD entries before flashing to the private, outside-Git
  `../local-artifacts/DP24_BD761844_sd_backup_2026-07-30.tar.gz`, SHA-256
  `290de3d1ece7a7f7bf7c434fb934e65304c5f3d12ba5554174b4489e9b4e0947`.
- Copied the CRC-named updater image, flushed it, read it back, and confirmed
  SHA-256 `6173b7682269a758fbc0908c1891c4ab41a77741165964eb5103456d3b2d6491`.
- Enumerated after install as `303a:8297`, manufacturer
  `CodexPad clean-room project`, product `duckyPad Pro Codex Controller`.
- Live report descriptor is 238 bytes and contains usage page `0xff00`,
  report ID 6, and 63-byte IN/OUT fields.
- Installed `packaging/70-codexpad-codex.rules` because Codex desktop's
  packaged rule grants Linux access only to production PID `8360`.
- Live `sys.version`, `device.status`, `v.oai.rgbcfg`, and `v.oai.thstatus`
  RPCs succeeded.
- 1,000 alternating version/status RPCs completed in 13.0 seconds with zero
  failures; diagnostic counters remained at zero.
- One malformed-channel frame was rejected without a reset; `rx_rejected`
  incremented to 1 as designed.
- Simulated USB deauthorize/reauthorize recovered enumeration and RPC.
- Captured paired press/release notifications for `AG00`–`AG05`,
  `ACT06`–`ACT12`, and `ENC_TO`; captured `ENC_CW`/`ENC_CC` encoder pulses
  and lower-encoder agent selection.
- Persisted `+` stock selection restored `0483:d11d`, truthful stock strings,
  the exact 205-byte stock report descriptor, and 16-byte IN endpoint.
- Persisted `-` selection returned to `303a:8297`; RPC and the local udev ACL
  recovered.
- Product decision: Codex-over-Bluetooth is deferred until after wired v1.
  Stock-mode BLE remains supported; Codex settings will report
  `Bluetooth: unavailable in wired release` instead of offering a dead pairing
  flow.

The 0.1.0 evidence is retained for provenance; the current pending list is in
the Known limitations and Next action sections above.
