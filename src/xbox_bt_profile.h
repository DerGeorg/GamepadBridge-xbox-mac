/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The real Xbox Wireless Controller's Bluetooth HID profile (045e:0b13).
 *
 * Why a copy of the real controller's descriptor instead of our own tidy one:
 * macOS recognises this vendor/product pair and then parses its reports as an
 * Xbox pad. A structurally different report is not rejected with an error —
 * it is silently dropped, which looks exactly like a controller that sends
 * nothing.
 *
 * The descriptor is the one dumped from a physical controller (283 bytes,
 * unified BLE firmware), byte for byte, minus one section: the Share button.
 * That section is the difference between a 17-byte and a 16-byte report, and
 * the length decides how SDL reads the buttons — see XboxBtButton.
 */

#pragma once

#include "output.h"
#include "../driverkit/shared/gamepad_report.h"

#include <stdint.h>
#include <string.h>

static const uint8_t kXboxBtReportDescriptor[] =
{
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x85, 0x01, 0x09, 0x01,
    0xa1, 0x00, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x27, 0xff,
    0xff, 0x00, 0x00, 0x95, 0x02, 0x75, 0x10, 0x81, 0x02, 0xc0,
    0x09, 0x01, 0xa1, 0x00, 0x09, 0x33, 0x09, 0x34, 0x15, 0x00,
    0x27, 0xff, 0xff, 0x00, 0x00, 0x95, 0x02, 0x75, 0x10, 0x81,
    0x02, 0xc0, 0x05, 0x01, 0x09, 0x32, 0x15, 0x00, 0x26, 0xff,
    0x03, 0x95, 0x01, 0x75, 0x0a, 0x81, 0x02, 0x15, 0x00, 0x25,
    0x00, 0x75, 0x06, 0x95, 0x01, 0x81, 0x03, 0x05, 0x01, 0x09,
    0x35, 0x15, 0x00, 0x26, 0xff, 0x03, 0x95, 0x01, 0x75, 0x0a,
    0x81, 0x02, 0x15, 0x00, 0x25, 0x00, 0x75, 0x06, 0x95, 0x01,
    0x81, 0x03, 0x05, 0x01, 0x09, 0x39, 0x15, 0x01, 0x25, 0x08,
    0x35, 0x00, 0x46, 0x3b, 0x01, 0x66, 0x14, 0x00, 0x75, 0x04,
    0x95, 0x01, 0x81, 0x42, 0x75, 0x04, 0x95, 0x01, 0x15, 0x00,
    0x25, 0x00, 0x35, 0x00, 0x45, 0x00, 0x65, 0x00, 0x81, 0x03,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x0c, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x0c, 0x81, 0x02, 0x15, 0x00, 0x25, 0x00,
    0x75, 0x01, 0x95, 0x04, 0x81, 0x03, 0x05, 0x0f, 0x09, 0x21,
    0x85, 0x03, 0xa1, 0x02, 0x09, 0x97, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x04, 0x95, 0x01, 0x91, 0x02, 0x15, 0x00, 0x25, 0x00,
    0x75, 0x04, 0x95, 0x01, 0x91, 0x03, 0x09, 0x70, 0x15, 0x00,
    0x25, 0x64, 0x75, 0x08, 0x95, 0x04, 0x91, 0x02, 0x09, 0x50,
    0x66, 0x01, 0x10, 0x55, 0x0e, 0x15, 0x00, 0x26, 0xff, 0x00,
    0x75, 0x08, 0x95, 0x01, 0x91, 0x02, 0x09, 0xa7, 0x15, 0x00,
    0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02, 0x65,
    0x00, 0x55, 0x00, 0x09, 0x7c, 0x15, 0x00, 0x26, 0xff, 0x00,
    0x75, 0x08, 0x95, 0x01, 0x91, 0x02, 0xc0, 0xc0
};

/*
 * Input report 0x01 — 16 bytes including the report ID:
 *
 *   0      report ID (always 1)
 *   1-4    X, Y            uint16, 0..65535, centre 32768, 0 = left / up
 *   5-8    Rx, Ry          uint16, same convention
 *   9-10   Z   (left trigger)   10 bits used, 0..1023, 6 bits padding
 *   11-12  Rz  (right trigger)  same
 *   13     hat switch      low nibble, 0 = centred (null), 1..8 = N..NW
 *   14-15  buttons         low 12 bits
 */
typedef struct __attribute__((packed))
{
    uint8_t  reportId;
    uint16_t leftX;
    uint16_t leftY;
    uint16_t rightX;
    uint16_t rightY;
    uint16_t leftTrigger;
    uint16_t rightTrigger;
    uint8_t  hat;
    uint16_t buttons;
} XboxBtReport;

/*
 * Button bit positions: consecutive, A to RS.
 *
 * Two kinds of reader see this report, and both ignore the button usages in
 * the descriptor and read fixed bit positions instead:
 *
 *   GameController.framework reads the button field consecutively — bit N is
 *   the Nth of A, B, X, Y, LB, RB, View, Menu, LS, RS. Measured with
 *   tools/gc-probe.m; a descriptor declaring different usages for the same
 *   bits changed nothing.
 *
 *   SDL — and with it Wine/CrossOver and Steam Input — picks one of two
 *   layouts by the LENGTH of the report (SDL_hidapi_xboxone.c,
 *   HIDAPI_DriverXboxOneBluetooth_HandleStatePacket):
 *
 *     size == 16   HandleButtons16   consecutive, the same as GameController
 *     size  > 16   HandleButtons     with gaps: X 0x08, Y 0x10, LB 0x40, ...
 *
 * With the Share button the report was 17 bytes, so SDL read it with gaps
 * while GameController read it consecutively: in PlateUp! under CrossOver, Y
 * arrived as X and Menu as RB, and the game could not be paused. Moving the
 * bits to the gapped positions fixed SDL and broke GameController the other
 * way round. Dropping the Share byte makes the report 16 bytes, and then both
 * readers agree.
 *
 * There is no Guide bit yet. Where macOS wants it is still an experiment — see
 * GAMEPADBRIDGE_GUIDE in output_corehid.cpp and the guide report below.
 */
enum XboxBtButton
{
    XBT_BTN_A    = 1u << 0,
    XBT_BTN_B    = 1u << 1,
    XBT_BTN_X    = 1u << 2,
    XBT_BTN_Y    = 1u << 3,
    XBT_BTN_LB   = 1u << 4,
    XBT_BTN_RB   = 1u << 5,
    XBT_BTN_VIEW = 1u << 6,
    XBT_BTN_MENU = 1u << 7,
    XBT_BTN_LS   = 1u << 8,
    XBT_BTN_RS   = 1u << 9,
};

/*
 * GamepadState -> the 16-byte report. Kept here rather than in the backend so
 * the test in test/xbox_bt/ exercises this exact code instead of a copy.
 */
inline uint16_t xboxBtAxisRight(int16_t value)
{
    return static_cast<uint16_t>(value + 32768);
}

// HID puts 0 at the top of the vertical axis; GamepadState keeps +Y = up.
inline uint16_t xboxBtAxisUp(int16_t value)
{
    return static_cast<uint16_t>(32767 - value);
}

inline XboxBtReport makeXboxBtReport(const GamepadState &s)
{
    XboxBtReport r;
    memset(&r, 0, sizeof(r));

    r.reportId = 0x01;

    r.leftX  = xboxBtAxisRight(s.stickLeftX);
    r.leftY  = xboxBtAxisUp(s.stickLeftY);
    r.rightX = xboxBtAxisRight(s.stickRightX);
    r.rightY = xboxBtAxisUp(s.stickRightY);

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

    return r;
}

/*
 * Output report 0x03 — rumble, 9 bytes including the report ID:
 *
 *   0      report ID (always 3)
 *   1      enable, low nibble: 0x01 right, 0x02 left, 0x04 RT, 0x08 LT
 *   2-5    magnitude LT, RT, left, right   0..100
 *   6      duration     in 10 ms steps
 *   7      start delay  in 10 ms steps
 *   8      loop count
 *
 * This is the "Set Effect Report" collection at the end of the descriptor.
 * SDL — Steam, and Wine/CrossOver with it — sends `03 0F LT RT L R FF 00 EB`
 * and a second one with zero magnitudes to stop.
 *
 * Whether the report ID arrives in front of the data or beside it depends on
 * the path the report took, so both shapes are accepted: nine bytes starting
 * with 0x03, or eight with the ID given separately.
 */
enum { XBT_RUMBLE_REPORT_ID = 0x03, XBT_RUMBLE_MAX = 100 };

inline bool parseXboxBtRumble(uint8_t reportId, const uint8_t *data,
                              size_t length, RumbleEffect &effect)
{
    if (length == 9 && data[0] == XBT_RUMBLE_REPORT_ID)
    {
        data++;
        length--;
    }

    else if (!(length == 8 && reportId == XBT_RUMBLE_REPORT_ID))
    {
        return false;
    }

    // The descriptor caps magnitudes at 100. Anything above that did not come
    // from a well-behaved sender, and the controller should not get it raw.
    auto magnitude = [](uint8_t value) {
        return static_cast<uint8_t>(value > XBT_RUMBLE_MAX ? XBT_RUMBLE_MAX
                                                           : value);
    };

    effect.enable       = data[0] & 0x0f;
    effect.leftTrigger  = magnitude(data[1]);
    effect.rightTrigger = magnitude(data[2]);
    effect.left         = magnitude(data[3]);
    effect.right        = magnitude(data[4]);
    effect.duration     = data[5];
    effect.delay        = data[6];
    effect.repeat       = data[7];

    return true;
}

/*
 * Input report 0x02 — the Xbox button on its own, for the experiment behind
 * GAMEPADBRIDGE_GUIDE=report.
 *
 * SDL reads a 16-byte pad's Xbox button from here and nowhere else
 * (HIDAPI_DriverXboxOneBluetooth_HandleGuidePacket: data[1] & 0x01). The
 * profile's descriptor has no report 2, so it has to be declared before it can
 * be sent: kXboxBtGuideItems goes in front of the descriptor's final End
 * Collection, inside the game pad rather than beside it, so the device still
 * has one top-level collection and is still classified as a game pad.
 */
static const uint8_t kXboxBtGuideItems[] =
{
    0x05, 0x0C,        // Usage Page (Consumer)
    0x85, 0x02,        // Report ID (2)
    0x0A, 0x23, 0x02,  // Usage (AC Home)
    0x15, 0x00,        // Logical Minimum (0)
    0x25, 0x01,        // Logical Maximum (1)
    0x75, 0x01,        // Report Size (1)
    0x95, 0x01,        // Report Count (1)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0x75, 0x07,        // Report Size (7)
    0x95, 0x01,        // Report Count (1)
    0x81, 0x03,        // Input (Const,Var,Abs)
};

typedef struct __attribute__((packed))
{
    uint8_t reportId;   // always 2
    uint8_t pressed;    // bit 0
} XboxBtGuideReport;
