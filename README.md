<div align="center">

<img src="packaging/icon/icon_1024.png" width="128" alt="GamepadBridge">

# GamepadBridge

**Use your Xbox wireless controller on a Mac.**<br>
The one macOS refuses to talk to.

[![Website](https://img.shields.io/badge/gamepadbridge.dergeorg.at-3DBF6E?style=for-the-badge)](https://gamepadbridge.dergeorg.at)
&nbsp;
[![Download](https://img.shields.io/badge/Download-GamepadBridge.dmg-111?style=for-the-badge)](https://gitlab.dergeorg.at/mac/gamepadbridge/-/releases)
&nbsp;
[![macOS](https://img.shields.io/badge/macOS-15%2B-111?style=for-the-badge&logo=apple&logoColor=white)](#requirements)
&nbsp;
[![License](https://img.shields.io/badge/GPL--2.0--or--later-777?style=for-the-badge)](LICENSE)

Signed &amp; notarized · Updates itself · No Homebrew required

</div>

> Development happens on
> [gitlab.dergeorg.at](https://gitlab.dergeorg.at/mac/gamepadbridge) — the
> GitHub repository is a mirror. Issues and merge requests belong on GitLab;
> releases appear in both places.

---

If you own an Xbox One controller and the little USB **Xbox Wireless
Adapter**, macOS is a dead end. It supports Bluetooth controllers only, and
does not recognise the adapter at all — no driver, no device, nothing. The
controller that works on every other machine you own is simply unusable.

GamepadBridge fixes that. Plug in the adapter, pair the controller, and it
shows up as a proper game controller: in System Settings, in Steam, in
emulators, in anything that reads a gamepad.

## Install

```sh
brew tap dergeorg/tap https://gitlab.dergeorg.at/mac/homebrew-tap.git
brew trust --cask dergeorg/tap/gamepadbridge
brew install --cask gamepadbridge
```

Or [download the disk image](https://gitlab.dergeorg.at/mac/gamepadbridge/-/releases)
and drag it to Applications. There is a walkthrough with pictures at
**[gamepadbridge.dergeorg.at](https://gamepadbridge.dergeorg.at)**.

The `brew trust` step is not boilerplate — a cask is executable Ruby, so
Homebrew refuses to load one from a third-party tap until you say you trust
it. [Read it first](https://gitlab.dergeorg.at/mac/homebrew-tap/-/blob/main/Casks/gamepadbridge.rb);
it is 50 lines.

## What you get

|  |  |
|---|---|
| 🎮 **A real controller** | Face buttons, bumpers, both analog triggers, both sticks, the d-pad and both stick clicks — recognised as an Xbox pad, not a generic joystick |
| 🖥️ **Lives in the menu bar** | Connection state, battery level, pairing, quit. No window, no Dock icon |
| 🔄 **Updates itself** | Signed updates through Sparkle, or `brew upgrade` if you installed that way |
| 🔒 **Signed and notarized** | Opens without a Gatekeeper detour. No `xattr` incantations |
| 📦 **Self-contained** | No Homebrew, no dependencies. Fetches the adapter firmware on first launch |
| 🧩 **Guided setup** | macOS needs two permissions and names one of them confusingly; the app walks you through both |

## Requirements

- **macOS 15** or newer (Apple silicon or Intel)
- **Xbox Wireless Adapter** — VID `0x045e`, PID `0x02e6` (original), `0x02fe`
  (slim) or `0x091e` (Surface)
- **A controller**: model 1537, 1697, 1698 (Elite), 1708 or 1797 (Elite 2)

A Bluetooth-capable controller paired over Bluetooth does not need any of
this — macOS handles those itself. This is for the adapter.

## Permissions

On first launch GamepadBridge asks for two permissions. It needs both to
publish the virtual gamepad, and without them the controller connects while
nothing receives its input.

- **Input Monitoring**
- **Accessibility** — macOS announces this one under a name that appears
  nowhere in the settings list. In German it says *"Gerätesteuerung und
  Datenzugriff"*. It means Accessibility.

Both are granted to whichever app *starts* the driver, so launch
GamepadBridge.app itself rather than its binary from a terminal. The app
opens the right panes for you, and its window stays on screen and ticks each
permission off as you grant it.

## How it works

```
Xbox controller ──802.11──▶ Wireless Adapter ──USB──▶ GamepadBridge
                                                            │
                                                         CoreHID
                                                            ▼
                                        virtual HID gamepad ──▶ your game
```

The adapter does not speak Bluetooth or HID — it runs a private 802.11 link
with Microsoft's GIP protocol on top, which is why macOS ignores it. That
protocol was reverse-engineered by **Medusalix** in
[xow](https://github.com/medusalix/xow) for Linux. GamepadBridge ports that
stack to macOS and publishes the result as a virtual HID device that the
system treats like any other controller.

Two things about macOS made that harder than a port should be, and both fail
silently: `GameController.framework` deliberately ignores virtual devices, and
it decodes controllers it recognises with its own layout rather than the one
they publish. [PORTING.md](docs/PORTING.md) has the details.

## Known limits

- **The Xbox button does nothing.** The Bluetooth profile macOS expects
  simply has no bit for it.
- **One controller at a time**, though the adapter supports four.
- **No rumble yet.** The protocol side exists; the plumbing does not.

## Building it yourself

See [docs/DEVELOPING.md](docs/DEVELOPING.md).

One caveat worth knowing before you try: the virtual gamepad needs Apple's
restricted `com.apple.developer.hid.virtual.device` entitlement, which comes
from a provisioning profile issued to a paid developer account. A build
signed with your own free account will compile and run, and then fail to
create the device without saying why. The released build is signed with that
entitlement, which is why installing it is the easy path.

## License and firmware

Code is **GPL-2.0-or-later**, derived from xow © Medusalix — see
[LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).

The adapter's **firmware is Microsoft's**, is not redistributable, and is not
part of this repository or the app. GamepadBridge downloads it from
Microsoft's own servers on first launch and verifies it against a known
checksum. It is covered by the Microsoft Terms of Use.

## Credits

Built entirely on the reverse-engineering work of **Medusalix** in
[xow](https://github.com/medusalix/xow) and
[xone](https://github.com/medusalix/xone). Without it none of this would
exist — the hard part was already done.
