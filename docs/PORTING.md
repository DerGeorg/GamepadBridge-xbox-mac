# Porting notes: xow (Linux) → macOS

## Key insight

xow is a **userspace** driver built on **libusb**. libusb is cross-platform,
so everything that talks to the dongle compiles on macOS unchanged. The
Linux-specific code is how the gamepad is exposed to the OS (the `uinput`
kernel interface) and the `signalfd`-based shutdown. Plus one macOS-specific
runtime rewrite that the protocol port forced out (see **USB threading**
below): xow reads the two bulk endpoints with synchronous libusb transfers on
two threads, which deadlocks on macOS. That's the ~15% we rewrote; the
protocol-heavy ~85% is reused as-is.

## USB threading (the macOS-specific rewrite)

xow runs two reader threads, each blocked in a **synchronous** libusb bulk
transfer, plus synchronous writes from those same threads. On Linux this is
fine. On macOS it **deadlocks within ~1 second**: libusb's darwin backend
lets only one thread service events at a time, so two threads each waiting in
a synchronous transfer wedge each other — one polls forever holding the event
lock, the other waits forever to acquire it. (`sample`(1) shows both stuck in
`sync_transfer_wait_for_completion`; one in `usbi_wait_for_events` → `poll`,
the other in `libusb_wait_for_event` → `pthread_cond_wait`.)

The fix, confirmed on hardware: **all libusb access happens on one thread.**

```
Dongle::usbThreadMain (single thread):
  addReader(EP5) / addReader(EP4)        async IN transfers, self-resubmitting
  loop:
    usbDevice->pumpEvents()              one event pump → reader callbacks
    drain inbound queue → handleBulkData (may issue writes — same thread, OK)
    drain task queue                     pairing posted from other threads
```

Reads are asynchronous (`libusb_fill_bulk_transfer` + `libusb_submit_transfer`);
their callbacks only enqueue bytes. Writes and control transfers stay
synchronous but only ever run on this one thread, so libusb is never touched
by two threads at once and the deadlock is structurally impossible. The MT76 /
GIP / controller code is unchanged.

## File-by-file

| xow file | Here | Action | Why |
|----------|------|--------|-----|
| `dongle/usb.{h,cpp}` | `src/dongle/usb.{h,cpp}` | **adapted** | libusb core kept; sync `bulkRead` from N threads replaced by async readers + a single-thread event pump (see **USB threading**). |
| `dongle/mt76.h` | `src/dongle/mt76.h` | **verbatim** | Chip/register/protocol defs. |
| `dongle/mt76.cpp` | `src/dongle/mt76.cpp` | **+ env override** | Firmware bring-up unchanged; added `XOW_FIRMWARE` path override. |
| `dongle/dongle.{h,cpp}` | `src/dongle/dongle.{h,cpp}` | **adapted** | Same 802.11/GIP routing & `handleBulkData`; two reader threads replaced by one USB thread + inbound/task queues. |
| `controller/gip.{h,cpp}` | `src/controller/gip.{h,cpp}` | **verbatim** | GIP handshake + input parsing; pure logic. |
| `utils/{bytes,buffer,log}.*` | `src/utils/…` | **verbatim** | Portable helpers. |
| `controller/controller.{h,cpp}` | `src/controller/controller.{h,cpp}` | **adapted** | Drop `uinput`/`ff_effect`; emit a neutral `GamepadState` to an `OutputDevice`. Constructor signature kept so `dongle.cpp` is untouched. |
| `controller/input.{h,cpp}` | — | **dropped** | Linux `uinput`. Replaced by `src/output*.cpp`. |
| `utils/reader.{h,cpp}` | — | **dropped** | Only used by `input.cpp` and the old `signalfd` loop. |
| `xow.cpp` | `src/main.cpp` | **rewritten** | `signalfd` → `sigwait`; same INT/TERM/USR1 semantics. |
| Makefile / install/* | `CMakeLists.txt`, `scripts/get-firmware.sh` | **replaced** | macOS build + firmware extraction (`shasum`, Homebrew `cabextract`). |
| — | `src/output.h` | **new** | OS-neutral output interface + `GamepadState`. |
| — | `src/output_console.cpp` | **new** | Stage 1 backend (terminal). |
| — | `src/output_driverkit.cpp` | **new** | Stage 2 backend (DriverKit variant). |
| — | `src/output_iohid.cpp` | **new** | Stage 2 backend via the `IOHIDUserDevice` SPI. |
| — | `src/output_corehid.cpp` + `src/corehid_shim.swift` | **new** | Stage 2 backend via CoreHID — the one that works. |
| — | `driverkit/**` | **new** | DriverKit virtual-gamepad extension scaffold. |

## The output abstraction

```
GIP input ─▶ Controller::inputReceived ─▶ GamepadState ─▶ OutputDevice::update
                                                              ├─ ConsoleOutput   (Stage 1)
                                                              └─ DriverKitOutput (Stage 2)
```

`OutputDevice` (in `src/output.h`) is the seam. The CMake option
`-DXOW_BACKEND=console|iohid|corehid|driverkit` selects which backend `.cpp`
provides the `makeOutputDevice()` factory, so the protocol core never changes.

## Verified on real hardware

Tested on **macOS 26.5.1 (arm64)** with the **Xbox Wireless Adapter `045e:02fe`**
("XBOX ACC") and an **Xbox One controller model 1537** (`045e:02d1`):

1. **libusb hotplug** — `getDevice` detects the dongle via `libusb_hotplug_*`. ✔
2. **Claiming the interface** — `libusb_set_configuration` + `claim_interface`
   succeed **without `sudo`**: macOS binds only the generic `IOUSBHostDevice`
   (no function driver), so libusb can claim interface 0 as a normal user. ✔
3. **Firmware + radio bring-up** — firmware uploads, MT76 inits, MAC reads back
   as `62:45:b5:…`. ✔
4. **Pairing + input** — controller pairs (button or `SIGUSR1`); A/B/X/Y,
   bumpers, Start/Select, D-pad, both analog triggers and both sticks (full
   −32768…32767 range) all report correctly and **stream continuously** with
   the single-thread USB model. ✔
5. **Endpoints** — `MT_EP_READ=5`, `MT_EP_READ_PACKET=4`, `MT_EP_WRITE=4` (from
   xow) hold unchanged. ✔

Notes / still open:

- **Stick Y sign** — kept as reported (+Y = up). Linux xow inverted it for the
  `uinput` convention; if a Stage 2 backend / game shows inverted Y, flip it
  there.
- **Stage 2 (real gamepad)** — working via the CoreHID backend (see below).
  The DriverKit variant remains scaffold-only and is not needed.

## Continuing the work

- Optional `output_cgevent.cpp`: keyboard/mouse fallback needing no Apple
  entitlements — useful for anyone without a paid membership.
- Notarised Developer ID build so others can install it without their own
  Apple account.
- Rumble (host → controller): `GipDevice::performRumble` already exists; wire
  a backend `RumbleCallback` to it for force feedback.

## Stage 2: publishing a virtual gamepad

Three routes exist on macOS; only one is both supported and app-level:

| Route | API | Verdict |
|-------|-----|---------|
| `IOHIDUserDevice` | C SPI in IOKit | Works, but undocumented. A dispatch queue **must** be set before `IOHIDUserDeviceActivate()` — otherwise it calls `os_crash` and aborts the process. |
| **CoreHID `HIDVirtualDevice`** | Swift, macOS 15+ | **Used here.** Supported, failable init tells you when creation is refused. |
| DriverKit dext | system extension | What Karabiner does. Needs its own Apple entitlement request; unnecessary for a virtual gamepad. |

CoreHID is Swift-only, so `src/corehid_shim.swift` wraps it behind a small C ABI
(`@_cdecl`) that the C++ core calls. Input reports are funnelled through an
`AsyncStream` rather than spawning a Task per report — they arrive at ~125 Hz
and their order matters. CMake enables the Swift language only for this backend
(`enable_language(Swift)` inside the backend branch), so the other backends keep
building with generators that have no Swift support.

### Two traps worth remembering

**A global `VERSION` macro breaks the SDK.** The build used to define
`VERSION="..."` on the command line. C++ never noticed, but Swift compiles the
whole IOKit Clang module, and `SCSICmds_INQUIRY_Definitions.h` declares
`UInt8 VERSION;` — which the preprocessor happily turned into
`UInt8 "f27db00";`. Macros injected build-wide must be namespaced; they are now
`GAMEPADBRIDGE_VERSION` / `GAMEPADBRIDGE_FIRMWARE`.

**Virtual HID creation fails silently.** When any prerequisite is missing, both
APIs simply return nil/NULL: no error code, no entitlement complaint, nothing in
the unified log beyond `[com.apple.iohid:userdevice] Destroy: <IOHIDUserDeviceRef
ref:0/0 id:0x0>`. The culprit in practice was TCC, attributed to the *terminal
app* that launched the binary rather than to the binary itself. See the Stage 2
checklist in the README.
