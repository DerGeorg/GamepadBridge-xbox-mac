/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Stage 2 output backend (macOS) — virtual HID gamepad via CoreHID.
 *
 * CoreHID (macOS 15+) is Apple's supported API for publishing a virtual HID
 * device and replaces the older IOHIDUserDevice SPI used by output_iohid.cpp,
 * which on macOS 27 refuses to create the device without reporting a reason
 * (it just returns NULL). CoreHID is Swift-only, so the actual calls live in
 * src/corehid_shim.swift behind the small C ABI declared below — including an
 * error channel, so a refusal shows up as an actual message.
 *
 * Needs the same entitlement as the IOHID backend:
 * "com.apple.developer.hid.virtual.device" (restricted — Apple assigns it to
 * your team, you enable it on the App ID, and the app is signed with a
 * provisioning profile that carries it).
 *
 * Build with:  cmake -DXOW_BACKEND=corehid -G Xcode ...   (see README)
 */

#include "output.h"
#include "utils/log.h"
#include "../driverkit/shared/gamepad_report.h"

#include <cstdint>
#include <cstring>
#include <cstdlib>

// Implemented in corehid_shim.swift.
extern "C" {
    int32_t gpb_corehid_create(const uint8_t *descriptor,
                               long descriptorLength,
                               uint32_t vendorId,
                               uint32_t productId,
                               uint64_t version,
                               const char *product);
    void gpb_corehid_send(const uint8_t *bytes, long length);
    int32_t gpb_corehid_take_error(char *buffer, long capacity);
    void gpb_corehid_destroy(void);
}

namespace
{
    /*
     * Diagnostics: the system refuses to create our virtual device without
     * giving a reason, so these two env vars isolate the two remaining
     * suspects. Both are debugging aids, not normal operation.
     *
     *   GAMEPADBRIDGE_MINIMAL_HID=1   use a textbook-minimal descriptor
     *                                 instead of our full Xbox layout
     *   GAMEPADBRIDGE_NEUTRAL_IDS=1   stop claiming Microsoft's VID/PID, in
     *                                 case macOS blocks impersonating the
     *                                 vendor it special-cases for Xbox pads
     */
    #define ENV_MINIMAL_HID "GAMEPADBRIDGE_MINIMAL_HID"
    #define ENV_NEUTRAL_IDS "GAMEPADBRIDGE_NEUTRAL_IDS"

    // Smallest descriptor that still is a game pad: 2 axes + 8 buttons,
    // three bytes per report.
    const uint8_t kMinimalDescriptor[] =
    {
        0x05, 0x01,       // Usage Page (Generic Desktop)
        0x09, 0x05,       // Usage (Game Pad)
        0xA1, 0x01,       // Collection (Application)
        0x05, 0x01,       //   Usage Page (Generic Desktop)
        0x09, 0x30,       //   Usage (X)
        0x09, 0x31,       //   Usage (Y)
        0x15, 0x81,       //   Logical Minimum (-127)
        0x25, 0x7F,       //   Logical Maximum (127)
        0x75, 0x08,       //   Report Size (8)
        0x95, 0x02,       //   Report Count (2)
        0x81, 0x02,       //   Input (Data,Var,Abs)
        0x05, 0x09,       //   Usage Page (Button)
        0x19, 0x01,       //   Usage Minimum (Button 1)
        0x29, 0x08,       //   Usage Maximum (Button 8)
        0x15, 0x00,       //   Logical Minimum (0)
        0x25, 0x01,       //   Logical Maximum (1)
        0x75, 0x01,       //   Report Size (1)
        0x95, 0x08,       //   Report Count (8)
        0x81, 0x02,       //   Input (Data,Var,Abs)
        0xC0              // End Collection
    };

    struct MinimalReport
    {
        int8_t x;
        int8_t y;
        uint8_t buttons;
    } __attribute__((packed));

    MinimalReport mapStateMinimal(const GamepadState &s)
    {
        MinimalReport r;
        std::memset(&r, 0, sizeof(r));

        r.x = static_cast<int8_t>(s.stickLeftX / 256);
        r.y = static_cast<int8_t>(s.stickLeftY / 256);

        uint8_t b = 0;
        if (s.a) b |= 1u << 0;
        if (s.b) b |= 1u << 1;
        if (s.x) b |= 1u << 2;
        if (s.y) b |= 1u << 3;
        r.buttons = b;

        return r;
    }

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
}

class CoreHidOutput : public OutputDevice
{
public:
    CoreHidOutput() : active(false) {}

    ~CoreHidOutput() override
    {
        if (active)
        {
            gpb_corehid_destroy();
        }
    }

    void create(const DeviceInfo &info) override
    {
        minimal = std::getenv(ENV_MINIMAL_HID) != nullptr;

        bool neutralIds = std::getenv(ENV_NEUTRAL_IDS) != nullptr;

        // Present as a Microsoft Xbox controller so GameController.framework
        // recognises it, same as the IOHID backend — unless we are bisecting.
        const char *name = neutralIds
            ? "GamepadBridge Test Pad"
            : (info.name.empty() ? "Xbox Wireless Controller" : info.name.c_str());

        // pid.codes test IDs, deliberately not Microsoft's.
        uint32_t vendorId  = neutralIds ? 0x1209 : (info.vendorId  ? info.vendorId  : 0x045E);
        uint32_t productId = neutralIds ? 0x0001 : (info.productId ? info.productId : 0x02D1);

        Log::info(
            "[corehid] mode: descriptor=%s ids=%04x:%04x (%s)",
            minimal ? "minimal" : "full",
            vendorId, productId,
            neutralIds ? "neutral" : "microsoft");

        int32_t result = gpb_corehid_create(
            minimal ? kMinimalDescriptor : kGamepadReportDescriptor,
            static_cast<long>(minimal ? sizeof(kMinimalDescriptor)
                                      : sizeof(kGamepadReportDescriptor)),
            vendorId,
            productId,
            info.version,
            name);

        if (result != 0)
        {
            // Pull the Swift-side detail across too, otherwise the reason is
            // stuck in the shim (update() never runs when creation failed).
            char message[512];

            if (gpb_corehid_take_error(message, sizeof(message)))
            {
                Log::error("[corehid] %s", message);
            }

            Log::error(
                "[corehid] Failed to create virtual gamepad (%d). The "
                "entitlement is present and valid, so the refusal is coming "
                "from elsewhere — check Privacy & Security permissions.",
                result);

            return;
        }

        active = true;

        Log::info("[corehid] Virtual gamepad created (presenting as %s)", name);
        Log::info("[corehid] Waiting for the first report to confirm it works...");
    }

    void update(const GamepadState &state) override
    {
        if (!active)
        {
            return;
        }

        if (minimal)
        {
            MinimalReport small = mapStateMinimal(state);

            gpb_corehid_send(
                reinterpret_cast<const uint8_t *>(&small),
                static_cast<long>(sizeof(small)));
        }

        else
        {
            GamepadReport report = mapState(state);

            gpb_corehid_send(
                reinterpret_cast<const uint8_t *>(&report),
                static_cast<long>(sizeof(report)));
        }

        // CoreHID reports refusals asynchronously; surface the first one.
        char message[512];

        if (gpb_corehid_take_error(message, sizeof(message)))
        {
            Log::error("[corehid] %s", message);
        }

        else if (!confirmed)
        {
            confirmed = true;

            Log::info("[corehid] Reports are flowing — gamepad is live.");
        }
    }

private:
    bool active;
    bool minimal = false;
    bool confirmed = false;
};

std::unique_ptr<OutputDevice> makeOutputDevice()
{
    return std::unique_ptr<OutputDevice>(new CoreHidOutput());
}
