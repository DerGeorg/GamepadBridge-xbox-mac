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
| **Stage 2** | Expose the input as a **real gamepad** games & emulators can use | ✅ **working** (macOS 27, CoreHID) — needs a paid Apple entitlement, see below |

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
games and emulators see a normal Xbox controller. Verified working on macOS 27:
the device shows up as `045e:02d1`, Usage Page 1 / Usage 5 (Game Pad),
Transport `Virtual`, with macOS binding its own `AppleUserHIDEventDriver` to it.

Backends:

- **`corehid`** (recommended) — uses CoreHID's `HIDVirtualDevice` (macOS 15+),
  Apple's supported API. A Swift shim (`src/corehid_shim.swift`) exposes it to
  the C++ core:
  ```sh
  cmake -G Xcode -DXOW_BACKEND=corehid -DXOW_MACOS_APP=ON \
        -DXOW_TEAM_ID=YOURTEAMID -S . -B build-corehid
  xcodebuild -project build-corehid/gamepadbridge.xcodeproj \
             -configuration Release -allowProvisioningUpdates
  ```
- **`iohid`** — the older `IOHIDUserDevice` SPI. Works, but undocumented and
  less reliable on macOS 27. Note: a dispatch queue **must** be set before
  `IOHIDUserDeviceActivate`, or the process aborts.
- **`driverkit`** — DriverKit system extension (scaffold in `driverkit/`), the
  route Karabiner takes. Not needed for the above to work.

### What it takes to run Stage 2

Publishing a virtual HID device needs **all** of these. Missing any one of them
makes device creation fail *silently* — both APIs just return nil/NULL with no
error anywhere in the system log.

1. **A paid Apple Developer membership.**
2. **The `com.apple.developer.hid.virtual.device` entitlement**, which is
   restricted: request it at
   <https://developer.apple.com/contact/request/system-extension/> and pick
   **"Virtual HID"** in the dropdown. Apple assigns it to your team manually.
3. **The capability enabled on your App ID** in Certificates, Identifiers &
   Profiles — being granted it for the team is not enough.
4. **Your Mac registered as a development device**, so Xcode can issue a
   development provisioning profile.
5. **Signing with that profile**, embedded in a `.app` bundle. A self-signed
   entitlement does not work: AMFI kills the process at launch under SIP.
6. ⚠️ **TCC permission for whichever app launches it.** This is the one that
   costs people hours. macOS attributes the request to the *responsible parent
   process*, so if you start it from a terminal, grant the permissions to
   **that terminal app** — iTerm if you use iTerm, Terminal if you use
   Terminal. Under **System Settings → Privacy & Security**, enable it for
   **Input Monitoring** (and Device Control / Accessibility if prompted).
   Launching the `.app` bundle directly instead makes macOS attribute the
   permission to GamepadBridge itself.

Running as root does **not** substitute for any of this.

Two diagnostic switches are built in, useful when creation is refused and you
need to bisect the cause:

| Env var | Effect |
|---------|--------|
| `GAMEPADBRIDGE_MINIMAL_HID=1` | use a textbook-minimal descriptor instead of the full Xbox layout |
| `GAMEPADBRIDGE_NEUTRAL_IDS=1` | advertise neutral pid.codes IDs instead of Microsoft's |

## What's tested — and what isn't

- ✅ Builds cleanly on **macOS 26.5.1 (arm64)** with AppleClang, libusb 1.0.30
  and CMake — `-Wall -Wextra`, **0 warnings**.
- ✅ Stage 1 **verified end-to-end on real hardware**: adapter `045e:02fe`
  detected, firmware loaded, MT76 radio brought up, controller **1537**
  (`045e:02d1`) paired; A/B/X/Y, bumpers, Start/Select, D-pad, both analog
  triggers and both sticks report correctly and **stream continuously** (no
  freeze). Clean shutdown via Ctrl-C including controller power-off.
- ✅ Runs **without `sudo`**.
- ✅ Stage 2 **verified on macOS 27**: with the entitlement granted and the
  TCC permission in place, the virtual gamepad is created and reports stream
  to it. `hidutil list` shows it as a Virtual transport game pad bound to
  `AppleUserHIDEventDriver`.

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
