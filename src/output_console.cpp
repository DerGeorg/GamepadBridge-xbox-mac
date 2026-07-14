/*
 * Copyright (C) 2026 GamepadBridge contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Stage 1 output backend.
 *
 * Does not create any virtual device. It simply prints the controller
 * state to the terminal. This is the milestone that proves the whole
 * dongle stack (USB enumeration, MT76 firmware load, 802.11 link, GIP
 * handshake, input parsing) works on macOS, independent of the much
 * harder "real gamepad" step.
 */

#include "output.h"
#include "utils/log.h"

#include <cstdio>
#include <string>

namespace
{
    // Render a pressed button as its letter, otherwise a dot.
    inline char flag(bool pressed, char symbol)
    {
        return pressed ? symbol : '.';
    }

    std::string dpad(const GamepadState &s)
    {
        std::string d;
        d += s.dpadUp ? 'U' : '.';
        d += s.dpadDown ? 'D' : '.';
        d += s.dpadLeft ? 'L' : '.';
        d += s.dpadRight ? 'R' : '.';
        return d;
    }
}

class ConsoleOutput : public OutputDevice
{
public:
    void create(const DeviceInfo &info) override
    {
        Log::info(
            "[console] Controller ready: %s (vid=%04x pid=%04x ver=%04x)",
            info.name.c_str(),
            info.vendorId,
            info.productId,
            info.version
        );
        Log::info("[console] Move sticks / press buttons to see live input:");
    }

    void update(const GamepadState &s) override
    {
        // Single, self-overwriting status line (carriage return, no newline).
        std::printf(
            "\r"
            "A%c B%c X%c Y%c | LB%c RB%c | %c%c | DP[%s] | "
            "LS(%6d,%6d) RS(%6d,%6d) | LT%4u RT%4u   ",
            flag(s.a, 'X'), flag(s.b, 'X'), flag(s.x, 'X'), flag(s.y, 'X'),
            flag(s.bumperLeft, 'X'), flag(s.bumperRight, 'X'),
            flag(s.start, 'S'), flag(s.select, 'B'),
            dpad(s).c_str(),
            s.stickLeftX, s.stickLeftY, s.stickRightX, s.stickRightY,
            s.triggerLeft, s.triggerRight
        );
        std::fflush(stdout);
    }
};

std::unique_ptr<OutputDevice> makeOutputDevice()
{
    return std::unique_ptr<OutputDevice>(new ConsoleOutput());
}
