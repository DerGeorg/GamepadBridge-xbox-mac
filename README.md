# GamepadBridge

Use Xbox wireless controllers (including **model 1537**) on a Mac **through the
Xbox Wireless Adapter (USB dongle)**.

There is no off-the-shelf solution: macOS only supports Bluetooth controllers
(model 1708 and newer) natively, and it does not recognize the dongle at all.
The entire dongle protocol already exists, though — reverse-engineered in
[**xow**](https://github.com/medusalix/xow) (Linux). GamepadBridge **ports xow's
dongle stack to macOS** and replaces the Linux-specific output (`uinput`) with a
swappable backend layer.

The lucky part: xow is pure **userspace + libusb**. So the hard part (USB, MT76
firmware, 802.11 link, GIP protocol) ports almost 1:1 — only the output had to
be rewritten.

## Two stages

| Stage | What | Status |
|-------|------|--------|
| **Stage 1** | Talk to the dongle, pair a controller, show input **in the terminal** | ✅ **done & tested on real hardware** (macOS 26.5.1 arm64, adapter `045e:02fe`, controller 1537) |
| **Stage 2** | Expose the input as a **real gamepad** games & emulators can use | 🟡 **backend written & compiles** — gated on an Apple entitlement (see below) |

Stage 1 proves the dongle works on the Mac. Stage 2 turns it into a controller
that games and emulators see.

## Supported hardware

- **Dongle**: Xbox Wireless Adapter, VID `0x045e`, PID `0x02e6` (old),
  `0x02fe` (new/slim), `0x091e` (Surface).
- **Controllers**: 1537, 1697, 1698 (Elite), 1708, 1797 (Elite 2) — same as
  xow. Model **1537 is explicitly listed as "Working" by xow**.

## Requirements

```sh
brew install libusb pkg-config cmake cabextract
```

## Stage 1 — build & run

```sh
# 1) Pull the firmware out of Microsoft's driver (not redistributable)
./scripts/get-firmware.sh
#    -> or to a custom path: ./scripts/get-firmware.sh ~/xow_dongle.bin

# 2) Build
cmake -S . -B build
cmake --build build

# 3) Run (plug in the dongle). No sudo needed on macOS:
./build/gamepadbridge
#    Custom firmware path? -> XOW_FIRMWARE=~/xow_dongle.bin ./build/gamepadbridge

# 4) Pair a controller: press the button on the dongle, then on the controller.
#    Or trigger pairing with a signal:  kill -USR1 $(pgrep gamepadbridge)
#    Quit: Ctrl-C (cleanly powers off the controller, radio & LED).
```

Once everything is up, a live status line with buttons, sticks and triggers
appears as soon as the controller connects.

### macOS notes

- **No root needed:** unlike Linux, macOS loads no driver of its own onto the
  dongle (only the generic `IOUSBHostDevice`), so libusb can claim interface 0
  as a normal user — `sudo` is not required. If you do hit
  `LIBUSB_ERROR_ACCESS`/`BUSY`: unplug/replug the dongle and make sure no
  second instance is running.
- **USB threading:** on macOS, xow's design (two reader threads doing
  synchronous libusb transfers) deadlocks within ~1 s. This port funnels all
  USB access through **one** thread with asynchronous reads — details in
  [`docs/PORTING.md`](docs/PORTING.md).
- **USB port:** as on Linux, some USB-3 ports misbehave; a (USB-2) hub in
  between helps with timeouts.

## Stage 2 — real gamepad

Stage 2 publishes a **virtual HID gamepad** that Stage 1 feeds with reports, so
games and emulators see a normal Xbox controller. Two backends are available:

- **`iohid`** (recommended) — creates the virtual device straight from
  userspace via `IOHIDUserDevice`. No system extension, no host app:
  ```sh
  cmake -DXOW_BACKEND=iohid -S . -B build-iohid
  cmake --build build-iohid
  ```
- **`driverkit`** — a full DriverKit system extension (scaffold in
  [`driverkit/`](driverkit/)). More moving parts; the alternative if you prefer
  the "blessed" distribution path.

**The gate (both backends):** publishing a virtual HID device requires a
*restricted* Apple entitlement — `com.apple.developer.hid.virtual.device` for
`iohid`, the DriverKit family entitlements for `driverkit`. Under SIP these are
only honored when backed by an Apple-issued provisioning profile, so a paid
Apple Developer account **and** Apple's approval of the entitlement request are
required. See [`driverkit/README.md`](driverkit/README.md) for the request
process and a no-entitlement fallback.

## What's tested — and what isn't

- ✅ Builds cleanly on **macOS 26.5.1 (arm64)** with AppleClang, libusb 1.0.30
  and CMake — `-Wall -Wextra`, **0 warnings**.
- ✅ Stage 1 **verified end-to-end on real hardware**: adapter `045e:02fe`
  detected, firmware loaded, MT76 radio brought up, controller **1537**
  (`045e:02d1`) paired; A/B/X/Y, bumpers, Start/Select, D-pad, both analog
  triggers and both sticks report correctly and **stream continuously** (no
  freeze). Clean shutdown via Ctrl-C including controller power-off.
- ✅ Runs **without `sudo`**.
- 🟡 Both Stage 2 backends compile; running one as a real gamepad is gated on
  the Apple entitlement (request pending).

## License & firmware

- Code: **GPL-2.0-or-later** (derived from xow, © Medusalix). See
  [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md).
- The dongle **firmware** is Microsoft's, is **not** part of this repo, and is
  subject to the Microsoft Terms of Use. `get-firmware.sh` fetches it directly
  from Microsoft when needed.

## Credits

Built entirely on the reverse-engineering work of **Medusalix** in
[xow](https://github.com/medusalix/xow) and
[xone](https://github.com/medusalix/xone). The architecture overview (which
file does what and what was ported) is in [`docs/PORTING.md`](docs/PORTING.md).
