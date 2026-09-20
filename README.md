# GamepadBridge

> Development happens on
> [gitlab.dergeorg.at](https://gitlab.dergeorg.at/mac/gamepadbridge) — the
> GitHub repository is a mirror. Issues and merge requests belong on GitLab;
> releases appear in both places.

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

## Install

Download [the latest release](https://gitlab.dergeorg.at/mac/gamepadbridge/-/releases),
drag **GamepadBridge.app** to Applications, and launch it. The app is signed
and notarized, so Gatekeeper lets it open without any detour through
System Settings.

Or with Homebrew:

```sh
brew tap dergeorg/tap https://gitlab.dergeorg.at/mac/homebrew-tap.git
brew trust --cask dergeorg/tap/gamepadbridge
brew install --cask gamepadbridge
```

The `brew trust` step is not boilerplate: a cask is executable Ruby, so
Homebrew refuses to load one from a third-party tap until you say you trust
it. Read [the cask](https://gitlab.dergeorg.at/mac/homebrew-tap/-/blob/main/Casks/gamepadbridge.rb)
first — it is 50 lines.

It runs as a **menu bar item**, not a window: the icon shows whether a
controller is connected, and the menu has the battery level, a pairing button,
an update check and Quit.

Updates install themselves through [Sparkle](https://sparkle-project.org):
the app asks on first launch whether it may check automatically, and the menu
has a manual check either way. Homebrew installs keep using `brew upgrade`
as usual.

On first launch it offers to download the adapter's firmware from Microsoft
(about 200 KB). That file is Microsoft's and cannot be bundled with the app;
it is verified against a known checksum and stored in
`~/Library/Application Support/GamepadBridge/`.

**Two** permissions are needed, and the app asks for both on first launch:

- **Input Monitoring**
- **Accessibility** — macOS announces this one under a name that appears
  nowhere in the settings list. In German it says *"Gerätesteuerung und
  Datenzugriff"*, which is `kTCCServiceAccessibility`, listed as
  Accessibility.

Both are easy to grant to the wrong thing: macOS attributes them to whichever
app *starts* the driver. Launch GamepadBridge.app itself, not its binary from
a terminal — otherwise the permissions belong to the terminal.

Without them the controller connects and nothing receives its input. The app
says so in the menu bar and reopens its permission window rather than leaving
you guessing.

Requires macOS 15 or newer (CoreHID) and an Apple Silicon or Intel Mac.

## Two stages

| Stage | What | Status |
|-------|------|--------|
| **Stage 1** | Talk to the dongle, pair a controller, show input **in the terminal** | ✅ **done & tested on real hardware** (macOS 26.5.1 arm64, adapter `045e:02fe`, controller 1537) |
| **Stage 2** | Expose the input as a **real gamepad** games & emulators can use | ✅ **working** (macOS 27, CoreHID) — visible to both the raw-HID path and `GameController.framework`. Needs a paid Apple entitlement, see below |

Stage 1 proves the dongle works on the Mac. Stage 2 turns it into a controller
that games and emulators see.

## Supported hardware

- **Dongle**: Xbox Wireless Adapter, VID `0x045e`, PID `0x02e6` (old),
  `0x02fe` (new/slim), `0x091e` (Surface).
- **Controllers**: 1537, 1697, 1698 (Elite), 1708, 1797 (Elite 2) — same as
  xow. Model **1537 is explicitly listed as "Working" by xow**.

## Requirements

Only needed to **build** GamepadBridge:

```sh
brew install libusb pkg-config cmake
```

A released build has no such dependency — libusb is linked statically, and
the firmware is extracted with the `bsdtar` that ships with macOS.

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

### Tuning what the pad claims to be

| Env var | Effect |
|---------|--------|
| `GAMEPADBRIDGE_IDS=` | identity preset: `xbox-bt` (default, `045e:0b13`), `xbox-usb` (`045e:02d1`), `x360` (`045e:028e`), `neutral` (pid.codes `1209:0001`) |
| `GAMEPADBRIDGE_TRANSPORT=` | `usb`, `bluetooth`, `ble`, `virtual`, or `none`; defaults to whatever matches the identity |
| `GAMEPADBRIDGE_MINIMAL_HID=1` | textbook-minimal descriptor instead of the full Xbox layout |
| `GAMEPADBRIDGE_SELFTEST=1` | publish the pad and wiggle it **without the dongle** — no hardware, no pairing |

`GAMEPADBRIDGE_NEUTRAL_IDS=1` still works as an alias for `GAMEPADBRIDGE_IDS=neutral`.

The self-test is the fast way to try combinations: it creates the virtual
gamepad, circles the left stick and cycles A/B/X/Y until Ctrl-C.

```sh
GAMEPADBRIDGE_SELFTEST=1 GAMEPADBRIDGE_TRANSPORT=usb \
    build-corehid/Release/gamepadbridge.app/Contents/MacOS/gamepadbridge
```

### Games and System Settings: the virtual-device filter

A virtual gamepad that works at the HID level is **not automatically visible to
games**. macOS has two separate paths:

| Path | Who uses it | Status |
|------|-------------|--------|
| Raw HID (IOKit `IOHIDManager`) | SDL games, emulators, browsers, most engines | ✅ works out of the box |
| `GameController.framework` | System Settings › Game Controllers, Apple-native ports | ✅ works — but only after clearing two hurdles |

The second path took two separate fixes, because it fails **silently** both
times:

1. **It ignores virtual devices.** Apple confirmed this is deliberate: the
   framework "is designed and tested for *real* (physical) game controllers"
   and has "existing checks […] to ignore virtual HID devices, specifically to
   prevent issues arising from looping game controller input back into the OS"
   ([Apple Developer Forums, thread 812774](https://developer.apple.com/forums/thread/812774)).
   A CoreHID device reports transport `Virtual` by default, so the pad never
   appeared. Claiming a physical transport is what gets it enumerated.
2. **It parses recognised controllers with its own layout.** Once macOS knows
   the device by vendor/product ID, it stops caring what our descriptor says
   and decodes reports the way the real controller sends them. Our own tidy
   15-byte report was discarded without a word — the pad showed up in System
   Settings and delivered nothing. `src/xbox_bt_profile.h` therefore publishes
   a byte-exact copy of the real Xbox Bluetooth descriptor (283 bytes) and a
   matching 17-byte report.

To tell the two paths apart when something breaks, use the bundled probes:

```sh
# what GameController.framework sees; "watch" prints live button names
clang -fobjc-arc -framework Foundation -framework GameController \
      -o /tmp/gc-probe tools/gc-probe.m && /tmp/gc-probe watch
```

```sh
# the raw HID path, independent of GameController
clang -framework IOKit -framework CoreFoundation \
      -o /tmp/hid-probe tools/hid-probe.c && /tmp/hid-probe
```

`gc-probe watch` is how the button bit order in `xbox_bt_profile.h` was
established. Don't do it by hand: `GAMEPADBRIDGE_SELFTEST=1` drives one input
at a time and announces each, so the mapping falls out of putting the two logs
side by side. Correlate them **by timestamp** — a step that lands on a bit
macOS doesn't use produces no event at all, so counting lines quietly shifts
everything after it and turns one wrong bit into a plausible-looking wrong
answer.

Everything maps except the **Xbox/Guide button**: this profile doesn't carry it
in the gamepad report, and the Consumer "Record" usage the descriptor also
declares never reaches GameController either. It is left unmapped rather than
reported as something it isn't.

Apple explicitly does not guarantee that presenting a virtual device this way
keeps working across macOS releases.

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
  to it. `hidutil list` shows it as a game pad bound to
  `AppleUserHIDEventDriver`, and an `IOHIDManager` client matches and opens it.
- ✅ Visible to `GameController.framework` as an Xbox One pad with a full
  `extendedGamepad` profile. All 21 inputs were swept individually and checked
  against what macOS reports: 20 map correctly (face buttons, bumpers,
  View/Menu, both stick clicks, all four d-pad directions, both sticks with
  correct orientation, both analog triggers). The Xbox button is the
  exception — see above.
- ✅ **Verified in an actual game** (Unrailed) on macOS 27.

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
