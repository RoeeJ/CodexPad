# Recovery and return to stock

The released recovery image is never overwritten:

```text
firmware/DPP_FW_3.3.5_7e717835.bin
size: 864560 bytes
SHA-256: e3266c0fde56d6103f612c5b36eb24ed09e448d413758615cf1f28a1c4ce01a4
```

## Preferred recovery through the boot escape

1. Hold both `+` and `-`.
2. Press reset, or reconnect USB while continuing to hold both buttons.
3. Release the buttons after the normal duckyPad startup screen appears.
4. Long-press `+` or `-`, select **Mount USB**, and mount the drive.
5. Copy `DPP_FW_3.3.5_7e717835.bin` to it unchanged.
6. Safely eject, reboot from the storage screen, and approve the update.
7. Verify normal enumeration as `0483:d11d` and firmware version 3.3.5.

The dual-path firmware also persists mode selection:

- hold `+` alone for at least 800 ms during boot to select and remember stock
  mode;
- hold `-` alone for at least 800 ms during boot to select and remember Codex
  mode;
- hold both to enter stock mode for that boot without changing the saved mode.

## ROM download recovery

If the application cannot boot:

1. Hold the physical RESET button.
2. Hold the upper encoder switch (BOOT/DFU).
3. Release RESET.
4. Release the encoder switch.
5. Confirm that an ESP32-S3 USB serial/download device appears.
6. From a known-good stock build, run `idf.py -p PORT flash`, or use Espressif
   tooling with that build's generated `flash_args`.
7. Reset the device and restore the released image through the normal updater
   if necessary.

The checked-in released `.bin` is an OTA application image. Do not write it to
flash offset zero as though it were a merged factory image.

## Recovery evidence still required

Both paths must be physically executed and recorded in
`implementation-status.md` before the firmware is described as recovery
validated.
