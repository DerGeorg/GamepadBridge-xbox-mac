# NOTICE

This project is a macOS port of **xow** and is therefore a derivative work
released under the **GNU General Public License, version 2 or later**.

## Upstream

- **xow** — Linux user-mode driver for the Xbox One wireless dongle
  Copyright (C) 2019–2021 Medusalix
  <https://github.com/medusalix/xow> — GPL-2.0

All of the hard reverse-engineering (MT76xx chip bring-up, firmware loading,
802.11 link handling, and the GIP — Game Input Protocol — packet formats) is
Medusalix's work. This port reuses that code and replaces only the Linux
output layer.

## File provenance

**Verbatim from xow** (unmodified except where noted):
`src/dongle/usb.{h,cpp}`, `src/dongle/dongle.{h,cpp}`, `src/dongle/mt76.h`,
`src/controller/gip.{h,cpp}`, `src/utils/bytes.h`, `src/utils/buffer.h`,
`src/utils/log.{h,cpp}`, `LICENSE`.

**Adapted from xow:**
- `src/dongle/mt76.cpp` — added an `XOW_FIRMWARE` environment-variable override
  for the firmware path.
- `src/controller/controller.{h,cpp}` — replaced the Linux `uinput`/force-
  feedback code with the OS-neutral `OutputDevice` abstraction.
- `src/main.cpp` — replaces xow's `xow.cpp`; Linux `signalfd` swapped for
  portable `sigwait`.

**New in this port:**
`src/output.h`, `src/output_console.cpp`, `src/output_driverkit.cpp`,
`driverkit/**`, `CMakeLists.txt`, `scripts/get-firmware.sh`, `docs/**`.

## Firmware

The Xbox Wireless Adapter firmware is proprietary to Microsoft, is **not**
included in this repository, and is governed by the Microsoft Terms of Use
(<https://www.microsoft.com/en-us/legal/terms-of-use>). `scripts/get-firmware.sh`
downloads it directly from Microsoft's servers at the user's request.
