/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Shared HID definitions for the virtual Xbox gamepad.
 *
 * Included by BOTH:
 *   - the DriverKit extension (publishes this report descriptor), and
 *   - the userspace backend (src/output_driverkit.cpp, fills these reports).
 *
 * Keeping the descriptor and the packed report struct in one file guarantees
 * the two sides never disagree on the byte layout.
 *
 * This header is intentionally dependency-free (no output.h, no IOKit).
 */

#pragma once

#include <stdint.h>

/*
 * HID input report (15 bytes, no report ID).
 *
 *   offset  size  field
 *   ------  ----  ---------------------------------------------
 *   0       2     left stick X    (int16,  -32768..32767)
 *   2       2     left stick Y    (int16,  -32768..32767, +up)
 *   4       2     right stick X   (int16,  -32768..32767)
 *   6       2     right stick Y   (int16,  -32768..32767, +up)
 *   8       2     left trigger    (uint16, 0..1023)
 *   10      2     right trigger   (uint16, 0..1023)
 *   12      1     hat / d-pad     (0 = centered, 1..8 = N..NW)
 *   13      2     buttons bitfield (11 buttons + 5 bits padding)
 */
typedef struct __attribute__((packed))
{
    int16_t  leftX;
    int16_t  leftY;
    int16_t  rightX;
    int16_t  rightY;
    uint16_t leftTrigger;
    uint16_t rightTrigger;
    uint8_t  hat;
    uint16_t buttons;
} GamepadReport;

// Button bit positions inside GamepadReport.buttons (matches descriptor order).
enum GamepadButton
{
    GP_BTN_A      = 1u << 0,
    GP_BTN_B      = 1u << 1,
    GP_BTN_X      = 1u << 2,
    GP_BTN_Y      = 1u << 3,
    GP_BTN_LB     = 1u << 4,   // left bumper
    GP_BTN_RB     = 1u << 5,   // right bumper
    GP_BTN_VIEW   = 1u << 6,   // "select" / view
    GP_BTN_MENU   = 1u << 7,   // "start" / menu
    GP_BTN_LS     = 1u << 8,   // left stick click
    GP_BTN_RS     = 1u << 9,   // right stick click
    GP_BTN_GUIDE  = 1u << 10,  // Xbox / guide button
};

// Hat switch values (8-way), 0 = centered (null state in the descriptor).
static inline uint8_t gamepad_hat(bool up, bool down, bool left, bool right)
{
    if (up && right)   return 2;
    if (down && right) return 4;
    if (down && left)  return 6;
    if (up && left)    return 8;
    if (up)            return 1;
    if (right)         return 3;
    if (down)          return 5;
    if (left)          return 7;
    return 0;
}

/*
 * HID report descriptor for an Xbox-style gamepad.
 *
 * Generic Desktop / Game Pad: 4 stick axes (X,Y,Rx,Ry), 2 trigger axes
 * (Z,Rz), an 8-way hat, and 11 buttons. This matches the GamepadReport
 * struct above byte-for-byte.
 */
static const uint8_t kGamepadReportDescriptor[] =
{
    0x05, 0x01,             // Usage Page (Generic Desktop)
    0x09, 0x05,             // Usage (Game Pad)
    0xA1, 0x01,             // Collection (Application)

    // --- Sticks: X, Y, Rx, Ry (16-bit signed) ---
    0x05, 0x01,             //   Usage Page (Generic Desktop)
    0x09, 0x30,             //   Usage (X)
    0x09, 0x31,             //   Usage (Y)
    0x09, 0x33,             //   Usage (Rx)
    0x09, 0x34,             //   Usage (Ry)
    0x16, 0x00, 0x80,       //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,       //   Logical Maximum (32767)
    0x75, 0x10,             //   Report Size (16)
    0x95, 0x04,             //   Report Count (4)
    0x81, 0x02,             //   Input (Data,Var,Abs)

    // --- Triggers: Z, Rz (16-bit, 0..1023) ---
    0x09, 0x32,             //   Usage (Z)
    0x09, 0x35,             //   Usage (Rz)
    0x15, 0x00,             //   Logical Minimum (0)
    0x26, 0xFF, 0x03,       //   Logical Maximum (1023)
    0x75, 0x10,             //   Report Size (16)
    0x95, 0x02,             //   Report Count (2)
    0x81, 0x02,             //   Input (Data,Var,Abs)

    // --- Hat switch (d-pad) ---
    0x09, 0x39,             //   Usage (Hat switch)
    0x15, 0x01,             //   Logical Minimum (1)
    0x25, 0x08,             //   Logical Maximum (8)
    0x35, 0x00,             //   Physical Minimum (0)
    0x46, 0x3B, 0x01,       //   Physical Maximum (315)
    0x65, 0x14,             //   Unit (Degrees, English Rotation)
    0x75, 0x08,             //   Report Size (8)
    0x95, 0x01,             //   Report Count (1)
    0x81, 0x42,             //   Input (Data,Var,Abs,Null State)
    0x65, 0x00,             //   Unit (None)

    // --- Buttons 1..11 ---
    0x05, 0x09,             //   Usage Page (Button)
    0x19, 0x01,             //   Usage Minimum (Button 1)
    0x29, 0x0B,             //   Usage Maximum (Button 11)
    0x15, 0x00,             //   Logical Minimum (0)
    0x25, 0x01,             //   Logical Maximum (1)
    0x75, 0x01,             //   Report Size (1)
    0x95, 0x0B,             //   Report Count (11)
    0x81, 0x02,             //   Input (Data,Var,Abs)

    // --- Padding (5 bits) ---
    0x75, 0x01,             //   Report Size (1)
    0x95, 0x05,             //   Report Count (5)
    0x81, 0x03,             //   Input (Const,Var,Abs)

    0xC0                    // End Collection
};
