# Stage 2 — Virtual Xbox gamepad (DriverKit)

Stage 1 proves the dongle works and prints input. Stage 2 makes that input
show up as a **real gamepad** that games and emulators can use. On modern
macOS (11+, Apple Silicon) the only sanctioned way to publish a virtual HID
device is a **DriverKit** system extension — kexts like `foohid` and the old
`360Controller` no longer load.

```
 dongle ──USB──▶ GamepadBridge (Stage 1 core)
                      │ GamepadState
                      ▼
            output_driverkit.cpp  ──IOKit user client──▶  XboxOneGamepadDriver (.dext)
                                                              │ handleReport()
                                                              ▼
                                                        macOS HID stack
                                                   (IOHIDManager / GameController)
```

## ⚠️ The honest part: this is gated by Apple

Building and *running* a virtual HID DriverKit extension requires:

1. **Apple Developer Program membership** (paid).
2. **Approval of a DriverKit entitlement request** from Apple for
   `com.apple.developer.driverkit` and `…driverkit.transport.hid`
   (request form: <https://developer.apple.com/contact/request/system-extension/>).
   These are *managed* entitlements — they only work when baked into a
   provisioning profile Apple issues you. **This approval is the real
   blocker** and can take time (or be declined for hobby use).
3. **Xcode** to build the `.dext` and embed it in a host app.
4. For end users: the extension must be **code-signed and notarized**, and
   the user approves it in System Settings → Privacy & Security.

For purely local experimentation you can `systemextensionsctl developer on`
to load a development-signed dext, but the DriverKit entitlement still has to
be on your signing profile — there is no way around the entitlement itself.

If Apple approval isn't an option, see "Fallback" at the bottom.

## What's in this folder

| File | Purpose | Status |
|------|---------|--------|
| `shared/gamepad_report.h` | HID report descriptor + packed report struct, shared with the userspace driver | ✅ complete, correct |
| `XboxOneGamepad/XboxOneGamepadDriver.iig` | DriverKit interface | 🟡 skeleton |
| `XboxOneGamepad/XboxOneGamepadDriver.cpp` | `IOUserHIDDevice` implementation (descriptor, device description, `postInputReport`) | 🟡 skeleton, not built |
| `XboxOneGamepad/Info.plist` | dext personality (matches `IOUserResources`) | 🟡 template, fill TEAMID/bundle id |
| `XboxOneGamepad/XboxOneGamepad.entitlements` | managed DriverKit entitlements | 🟡 template |
| `../src/output_driverkit.cpp` | userspace backend: maps state → report, sends via IOKit | 🟡 mapping done, transport scaffolded |

## Build outline

1. In Xcode: **File ▸ New ▸ Target ▸ Driver Extension**. Name it
   `XboxOneGamepad`. This generates `.iig`/`.cpp`/`Info.plist`/entitlements —
   replace them with the files here (merge the generated boilerplate).
2. Add a **companion user-client class** (see below) — the one piece not yet
   written out.
3. Set the target's entitlements to `XboxOneGamepad.entitlements`, set your
   Team, and select the provisioning profile that carries the DriverKit
   entitlement.
4. Embed the dext in a small **host app** (a normal macOS app target) that
   calls `OSSystemExtensionRequest.activationRequest(...)`. The host app needs
   `com.apple.developer.system-extension.install`.
5. Run the host app once to activate the extension (approve in System
   Settings). Verify with `systemextensionsctl list`.
6. Build the Stage 1 driver with the DriverKit backend and run it:
   ```sh
   cmake -DXOW_BACKEND=driverkit -S . -B build-dk && cmake --build build-dk
   sudo ./build-dk/gamepadbridge
   ```

## The missing piece: the user client

`XboxOneGamepadDriver::NewUserClient` must return an `IOUserClient` subclass
whose `externalMethod` (selector `kGamepadMethodSendReport = 0`) calls
`provider->postInputReport(input.structureInput, input.structureInputSize)`.
That's ~60 lines of boilerplate (`XboxOneGamepadUserClient.iig/.cpp`). It was
left out because its exact form depends on your Xcode/SDK version; Apple's
*"Communicating between a DriverKit extension and a client app"* sample is the
canonical reference. The selector number and the struct size already match
`src/output_driverkit.cpp`.

## Game compatibility

The virtual device advertises Microsoft's vendor ID (`0x045E`) and an Xbox
product ID. This matters:

- **SDL2 / IOHIDManager software** (RetroArch, Dolphin, melonDS, Steam with
  Steam Input, …) sees any well-formed HID gamepad — these should "just work".
- **Apple's GameController.framework** (many native Mac App Store / Apple
  Arcade titles) maps controllers it recognizes by VID/PID to the Xbox
  profile, which is why we present Microsoft IDs. Some MFi-strict contexts may
  still differ; test with your target games.

## Fallback if DriverKit is blocked

If you can't get the DriverKit entitlement, the only no-entitlement route is
**keyboard/mouse emulation** via `CGEventPost` (Quartz Accessibility) — like
the `golden-narwhal12/xbox-controller-driver-macos` project does. That makes
the controller drive WASD/mouse, **not** a real gamepad. You'd add an
`output_cgevent.cpp` backend implementing the same `OutputDevice` interface.
It's a downgrade from a real controller, but needs no Apple approval.
