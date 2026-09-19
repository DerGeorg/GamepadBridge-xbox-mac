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
#include "xbox_bt_profile.h"

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstring>
#include <string>

// Implemented in corehid_shim.swift.
extern "C" {
    int32_t gpb_corehid_create(const uint8_t *descriptor,
                               long descriptorLength,
                               uint32_t vendorId,
                               uint32_t productId,
                               uint64_t version,
                               const char *product,
                               const char *manufacturer,
                               const char *transport);
    void gpb_corehid_send(const uint8_t *bytes, long length);
    int32_t gpb_corehid_take_error(char *buffer, long capacity);
    void gpb_corehid_destroy(void);
}

namespace
{
    /*
     * Which identity the virtual pad claims, and over which transport.
     *
     * macOS only routes a HID device into GameController.framework (and thus
     * into System Settings > Game Controllers and into games) when it both
     * recognises the vendor/product pair and does not consider the device
     * virtual: Apple confirmed that the framework deliberately skips virtual
     * HID devices so synthesised input cannot be looped back into the OS.
     * Both are therefore tunable without a rebuild.
     *
     *   GAMEPADBRIDGE_IDS=<preset>    identity to claim (see kIdentities)
     *   GAMEPADBRIDGE_TRANSPORT=usb|bluetooth|ble|virtual|none
     *   GAMEPADBRIDGE_MINIMAL_HID=1   textbook-minimal descriptor instead of
     *                                 our full Xbox layout (descriptor bisect)
     */
    #define ENV_MINIMAL_HID "GAMEPADBRIDGE_MINIMAL_HID"
    #define ENV_IDS         "GAMEPADBRIDGE_IDS"
    #define ENV_TRANSPORT   "GAMEPADBRIDGE_TRANSPORT"
    // Superseded by GAMEPADBRIDGE_IDS=neutral; still honoured.
    #define ENV_NEUTRAL_IDS "GAMEPADBRIDGE_NEUTRAL_IDS"

    // Which report layout to publish. Generic is our own tidy descriptor;
    // XboxBt is a byte-exact copy of the real controller's, needed because
    // macOS parses recognised Xbox pads with its own fixed layout.
    enum Profile { ProfileGeneric, ProfileXboxBt };

    struct Identity
    {
        const char *preset;
        uint32_t    vendorId;
        uint32_t    productId;
        const char *manufacturer;
        const char *product;
        const char *transport;
        Profile     profile;
    };

    static_assert(sizeof(XboxBtReport) == 17,
                  "Xbox Bluetooth input report must stay 17 bytes");

    /*
     * "xbox-bt" is the default because it is the only Xbox controller macOS
     * supports natively: the Bluetooth firmware shared by the Xbox One S,
     * Series X|S and Elite 2 pads. 0x02D1 (what the dongle actually reports)
     * is the *wired* pad, which speaks GIP rather than HID, so macOS has no
     * profile for it at all.
     */
    const Identity kIdentities[] =
    {
        { "xbox-bt",  0x045E, 0x0B13, "Microsoft",    "Xbox Wireless Controller",     "bluetooth", ProfileXboxBt  },
        { "xbox-usb", 0x045E, 0x02D1, "Microsoft",    "Xbox One Wireless Controller", "usb",       ProfileGeneric },
        { "x360",     0x045E, 0x028E, "Microsoft",    "Xbox 360 Controller",          "usb",       ProfileGeneric },
        { "neutral",  0x1209, 0x0001, "GamepadBridge","GamepadBridge Test Pad",       "usb",       ProfileGeneric },
    };

    const Identity &resolveIdentity(const std::string &preset)
    {
        for (const Identity &candidate : kIdentities)
        {
            if (preset == candidate.preset)
            {
                return candidate;
            }
        }

        Log::error("[corehid] Unknown " ENV_IDS " '%s', falling back to '%s'",
                   preset.c_str(), kIdentities[0].preset);

        return kIdentities[0];
    }

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

    /*
     * HID axis convention is the opposite of ours on the vertical: 0 is up,
     * 65535 is down, whereas GamepadState keeps the controller's own +Y = up.
     */
    inline uint16_t axisUp(int16_t value)
    {
        return static_cast<uint16_t>(32767 - value);
    }

    inline uint16_t axisRight(int16_t value)
    {
        return static_cast<uint16_t>(value + 32768);
    }

    XboxBtReport mapStateXboxBt(const GamepadState &s)
    {
        XboxBtReport r;
        std::memset(&r, 0, sizeof(r));

        r.reportId = 0x01;

        r.leftX  = axisRight(s.stickLeftX);
        r.leftY  = axisUp(s.stickLeftY);
        r.rightX = axisRight(s.stickRightX);
        r.rightY = axisUp(s.stickRightY);

        r.leftTrigger  = s.triggerLeft;
        r.rightTrigger = s.triggerRight;

        r.hat = gamepad_hat(s.dpadUp, s.dpadDown, s.dpadLeft, s.dpadRight);

        uint16_t b = 0;
        if (s.a)           b |= XBT_BTN_A;
        if (s.b)           b |= XBT_BTN_B;
        if (s.x)           b |= XBT_BTN_X;
        if (s.y)           b |= XBT_BTN_Y;
        if (s.bumperLeft)  b |= XBT_BTN_LB;
        if (s.bumperRight) b |= XBT_BTN_RB;
        if (s.select)      b |= XBT_BTN_VIEW;
        if (s.start)       b |= XBT_BTN_MENU;
        if (s.thumbLeft)   b |= XBT_BTN_LS;
        if (s.thumbRight)  b |= XBT_BTN_RS;
        r.buttons = b;

        // s.guide has nowhere to go: neither a button bit nor the Consumer
        // "Record" usage reaches GameController, so the Xbox button stays
        // unmapped rather than being reported as something it is not.

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

        const char *preset = std::getenv(ENV_IDS);

        if (!preset && std::getenv(ENV_NEUTRAL_IDS))
        {
            preset = "neutral";
        }

        const Identity &identity = resolveIdentity(preset ? preset : "xbox-bt");

        // An explicit transport wins; otherwise take the one that matches the
        // identity, because a Bluetooth-only product ID arriving over "usb"
        // is exactly the kind of mismatch the OS may sanity-check.
        const char *transport = std::getenv(ENV_TRANSPORT);

        if (!transport || !*transport)
        {
            transport = identity.transport;
        }

        // The minimal descriptor is a bisecting aid and overrides the profile.
        profile = minimal ? ProfileGeneric : identity.profile;

        const uint8_t *descriptor = kGamepadReportDescriptor;
        size_t descriptorLength = sizeof(kGamepadReportDescriptor);
        const char *shape = "generic";

        if (minimal)
        {
            descriptor = kMinimalDescriptor;
            descriptorLength = sizeof(kMinimalDescriptor);
            shape = "minimal";
        }

        else if (profile == ProfileXboxBt)
        {
            descriptor = kXboxBtReportDescriptor;
            descriptorLength = sizeof(kXboxBtReportDescriptor);
            shape = "xbox-bt";
        }

        Log::info(
            "[corehid] mode: ids=%s (%04x:%04x) transport=%s descriptor=%s",
            identity.preset,
            identity.vendorId, identity.productId,
            transport,
            shape);

        int32_t result = gpb_corehid_create(
            descriptor,
            static_cast<long>(descriptorLength),
            identity.vendorId,
            identity.productId,
            info.version,
            identity.product,
            identity.manufacturer,
            transport);

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

        Log::info("[corehid] Virtual gamepad created (presenting as %s)",
                  identity.product);
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

        else if (profile == ProfileXboxBt)
        {
            XboxBtReport report = mapStateXboxBt(state);

            gpb_corehid_send(
                reinterpret_cast<const uint8_t *>(&report),
                static_cast<long>(sizeof(report)));
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
    Profile profile = ProfileGeneric;
};

std::unique_ptr<OutputDevice> makeOutputDevice()
{
    return std::unique_ptr<OutputDevice>(new CoreHidOutput());
}
