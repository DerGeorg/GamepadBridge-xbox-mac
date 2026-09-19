/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Stage 2 output backend (macOS) — virtual HID gamepad via IOHIDUserDevice.
 *
 * Unlike the DriverKit backend (output_driverkit.cpp), this needs no system
 * extension, no host app and no .dext: IOHIDUserDeviceCreateWithProperties()
 * publishes a virtual IOHIDDevice straight from userspace. The OS, every
 * IOHIDManager/SDL based app (RetroArch, Dolphin, Steam Input, …) and — via
 * the advertised Microsoft VID/PID — Apple's GameController.framework then see
 * a real Xbox-style gamepad.
 *
 * Gate: IOHIDUserDeviceCreateWithProperties requires the entitlement
 * "com.apple.developer.hid.virtual.device", which is restricted — Apple has to
 * assign it to your team and you enable it on the App ID. Verified on macOS:
 * running as root does NOT bypass it, and a self-signed entitlement is rejected
 * under SIP (AMFI kills the process on launch). Working setups are:
 *   - signed with a provisioning profile carrying the entitlement (paid
 *     Apple Developer account), OR
 *   - reduced SIP + ad-hoc sign with the entitlement.
 * If the device can't be created we log why and keep running as a no-op so the
 * rest of the driver (USB, pairing) is unaffected.
 *
 * Build with:  cmake -DXOW_BACKEND=iohid -S . -B build
 */

#include "output.h"
#include "utils/log.h"
#include "../driverkit/shared/gamepad_report.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hidsystem/IOHIDUserDevice.h>
#include <mach/mach_time.h>
#include <dispatch/dispatch.h>

#include <cstring>

namespace
{
    GamepadReport mapState(const GamepadState &s)
    {
        GamepadReport r;
        std::memset(&r, 0, sizeof(r));

        r.leftX  = s.stickLeftX;
        r.leftY  = s.stickLeftY;
        r.rightX = s.stickRightX;
        r.rightY = s.stickRightY;
        r.leftTrigger  = s.triggerLeft;
        r.rightTrigger = s.triggerRight;

        r.hat = gamepad_hat(s.dpadUp, s.dpadDown, s.dpadLeft, s.dpadRight);

        uint16_t b = 0;
        if (s.a)           b |= GP_BTN_A;
        if (s.b)           b |= GP_BTN_B;
        if (s.x)           b |= GP_BTN_X;
        if (s.y)           b |= GP_BTN_Y;
        if (s.bumperLeft)  b |= GP_BTN_LB;
        if (s.bumperRight) b |= GP_BTN_RB;
        if (s.select)      b |= GP_BTN_VIEW;
        if (s.start)       b |= GP_BTN_MENU;
        if (s.thumbLeft)   b |= GP_BTN_LS;
        if (s.thumbRight)  b |= GP_BTN_RS;
        if (s.guide)       b |= GP_BTN_GUIDE;
        r.buttons = b;

        return r;
    }

    // Helper: add an integer property to a dictionary.
    void setNumber(CFMutableDictionaryRef dict, const char *key, int value)
    {
        CFStringRef k = CFStringCreateWithCString(
            nullptr, key, kCFStringEncodingUTF8);
        CFNumberRef n = CFNumberCreate(nullptr, kCFNumberIntType, &value);
        CFDictionarySetValue(dict, k, n);
        CFRelease(n);
        CFRelease(k);
    }
}

class IoHidOutput : public OutputDevice
{
public:
    IoHidOutput() : device(nullptr), queue(nullptr) {}

    ~IoHidOutput() override
    {
        if (device)
        {
            // IOHIDUserDevice.h: the device must be cancelled before it is
            // released. The cancel handler installed in create() performs the
            // CFRelease once the queue has drained.
            IOHIDUserDeviceCancel(device);

            device = nullptr;
        }
    }

    void create(const DeviceInfo &info) override
    {
        CFMutableDictionaryRef props = CFDictionaryCreateMutable(
            nullptr, 0,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);

        CFDataRef descriptor = CFDataCreate(
            nullptr,
            kGamepadReportDescriptor,
            sizeof(kGamepadReportDescriptor));
        CFDictionarySetValue(
            props, CFSTR(kIOHIDReportDescriptorKey), descriptor);
        CFRelease(descriptor);

        // Present as a Microsoft Xbox controller so GameController.framework
        // recognises it. info.productId already carries the real PID (or the
        // 360-compat PID when XOW_COMPATIBILITY is set).
        setNumber(props, kIOHIDVendorIDKey,
                  info.vendorId ? info.vendorId : 0x045E);
        setNumber(props, kIOHIDProductIDKey,
                  info.productId ? info.productId : 0x02D1);
        setNumber(props, kIOHIDVersionNumberKey, info.version);
        setNumber(props, kIOHIDPrimaryUsagePageKey, 0x01); // Generic Desktop
        setNumber(props, kIOHIDPrimaryUsageKey, 0x05);     // Game Pad

        CFStringRef name = CFStringCreateWithCString(
            nullptr,
            info.name.empty() ? "Xbox Wireless Controller" : info.name.c_str(),
            kCFStringEncodingUTF8);
        CFDictionarySetValue(props, CFSTR(kIOHIDProductKey), name);
        CFRelease(name);

        device = IOHIDUserDeviceCreateWithProperties(nullptr, props, 0);

        CFRelease(props);

        if (!device)
        {
            Log::error(
                "[iohid] Could not create virtual gamepad. The "
                "'com.apple.developer.hid.virtual.device' entitlement is "
                "required — sign with a provisioning profile that carries it "
                "(Apple must assign the capability to your team), or use "
                "reduced SIP. Running as root does NOT help. "
                "See driverkit/README.md.");

            return;
        }

        // A dispatch queue MUST be set before activating: IOHIDUserDeviceActivate
        // aborts the process (os_crash) when the device has no queue. The cancel
        // handler releases the device once the queue has drained — see the
        // documented Activate/Cancel contract in IOHIDUserDevice.h.
        queue = dispatch_queue_create(
            "at.dergeorg.gamepadbridge.hid", DISPATCH_QUEUE_SERIAL);

        IOHIDUserDeviceSetDispatchQueue(device, queue);

        IOHIDUserDeviceRef cancelled = device;

        IOHIDUserDeviceSetCancelHandler(device, ^{
            CFRelease(cancelled);
        });

        IOHIDUserDeviceActivate(device);

        Log::info(
            "[iohid] Virtual gamepad ready (presenting as %s)",
            info.name.c_str());
    }

    void update(const GamepadState &state) override
    {
        if (!device)
        {
            return;
        }

        GamepadReport report = mapState(state);

        IOReturn ret = IOHIDUserDeviceHandleReportWithTimeStamp(
            device,
            mach_absolute_time(),
            reinterpret_cast<const uint8_t *>(&report),
            sizeof(report));

        if (ret != kIOReturnSuccess)
        {
            Log::error("[iohid] HandleReport failed: 0x%08x", ret);
        }
    }

private:
    IOHIDUserDeviceRef device;

    // Serial queue the HID device delivers its asynchronous events on. It
    // outlives the device (the cancel handler runs on it), so it is kept for
    // the lifetime of the process.
    dispatch_queue_t queue;
};

std::unique_ptr<OutputDevice> makeOutputDevice()
{
    return std::unique_ptr<OutputDevice>(new IoHidOutput());
}
