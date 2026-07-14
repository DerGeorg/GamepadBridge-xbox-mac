/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Stage 2 output backend (macOS only) — SCAFFOLD.
 *
 * Maps the normalized GamepadState onto the HID input report defined in
 * driverkit/shared/gamepad_report.h and hands it to the DriverKit virtual
 * gamepad extension through an IOKit user client.
 *
 * STATUS: The state->report mapping below is complete and correct. The
 * IOKit transport is real, idiomatic code but is marked with TODOs where
 * the exact user-client selector must match the .iig you ship in
 * driverkit/XboxOneGamepad. It has NOT been compiled or run — building it
 * requires Xcode + the macOS SDK and the activated system extension.
 * See driverkit/README.md.
 *
 * Build with:  cmake -DXOW_BACKEND=driverkit ..
 */

#include "output.h"
#include "utils/log.h"
#include "../driverkit/shared/gamepad_report.h"

#include <IOKit/IOKitLib.h>

#include <cstring>

// Must match kIOUserClientCreatorKey / IOUserClass exposed by the dext.
#define VIRTUAL_GAMEPAD_SERVICE "XboxOneGamepadDriver"

// User client selectors — MUST match the dext's externalMethod dispatch.
enum
{
    kGamepadMethodSendReport = 0,
};

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
}

class DriverKitOutput : public OutputDevice
{
public:
    DriverKitOutput() : connection(IO_OBJECT_NULL) {}

    ~DriverKitOutput() override
    {
        if (connection != IO_OBJECT_NULL)
        {
            IOServiceClose(connection);
        }
    }

    void create(const DeviceInfo &info) override
    {
        // Find the published virtual-gamepad service.
        io_service_t service = IOServiceGetMatchingService(
            kIOMainPortDefault,
            IOServiceMatching(VIRTUAL_GAMEPAD_SERVICE)
        );

        if (service == IO_OBJECT_NULL)
        {
            Log::error(
                "[driverkit] Virtual gamepad service not found. "
                "Is the system extension activated? (see driverkit/README.md)"
            );

            return;
        }

        kern_return_t kr = IOServiceOpen(
            service,
            mach_task_self(),
            0,
            &connection
        );

        IOObjectRelease(service);

        if (kr != KERN_SUCCESS)
        {
            Log::error("[driverkit] IOServiceOpen failed: 0x%08x", kr);

            return;
        }

        Log::info(
            "[driverkit] Virtual gamepad ready (presenting as %s)",
            info.name.c_str()
        );

        // TODO: optionally pass DeviceInfo (vid/pid/version/name) to the
        // dext here via a separate selector so the published HID device
        // advertises Microsoft's VID (0x045e) + an Xbox PID. GameController
        // .framework treats those IDs as a real Xbox controller.
    }

    void update(const GamepadState &state) override
    {
        if (connection == IO_OBJECT_NULL)
        {
            return;
        }

        GamepadReport report = mapState(state);

        // Send the raw report struct to the dext, which forwards it as a
        // HID input report via handleReport().
        kern_return_t kr = IOConnectCallStructMethod(
            connection,
            kGamepadMethodSendReport,
            &report,
            sizeof(report),
            nullptr,
            nullptr
        );

        if (kr != KERN_SUCCESS)
        {
            Log::error("[driverkit] sendReport failed: 0x%08x", kr);
        }
    }

private:
    io_connect_t connection;
};

std::unique_ptr<OutputDevice> makeOutputDevice()
{
    return std::unique_ptr<OutputDevice>(new DriverKitOutput());
}
