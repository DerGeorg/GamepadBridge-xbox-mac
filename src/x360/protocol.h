/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The Xbox 360 Wireless Receiver's packets, in and out.
 *
 * The layout is the one Linux's xpad driver uses. Every byte of it was checked
 * on this Mac against a receiver (045e:0719) and a controller with
 * tools/x360-probe.c, and test/x360/main.cpp holds the recorded packets.
 *
 * The receiver has four controller slots, each an interface of its own
 * (class ff, subclass 5d, protocol 81) with an interrupt endpoint each way.
 * Every packet names nothing but its content: which slot it belongs to is
 * the endpoint it arrived on.
 */

#pragma once

#include "../output.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace X360
{
    enum
    {
        SLOT_COUNT = 4,

        // What the receiver's own class descriptor declares: 29 bytes in,
        // 12 out. Reads get room for the endpoint's full 32.
        PACKET_SIZE = 32,
        COMMAND_SIZE = 12,
    };

    // The receivers xpad knows; 0x0719 is "Xbox 360 Wireless Receiver for
    // Windows", the common one, and the one this was tested with.
    const uint16_t VENDOR_ID = 0x045e;
    const uint16_t PRODUCT_IDS[] = { 0x0719, 0x0291, 0x02a9 };

    using Command = std::array<uint8_t, COMMAND_SIZE>;

    enum class Kind
    {
        // 08 80 when a controller connects to the slot, 08 00 when it leaves
        // (or when the slot is asked and is empty). Comes once, not repeatedly.
        Presence,
        // 00 01 00 f0 00 13, then the pad: every change of any input.
        Input,
        // 00 0f 00 f0, right after a controller connects: carries its identity.
        Link,
        // 00 00 00 f0 after each input, 00 f8 0x 00 every few seconds: link
        // upkeep. Nothing a pad needs.
        Other,
    };

    inline Kind classify(const uint8_t *data, size_t length)
    {
        if (length >= 2 && (data[0] & 0x08))
        {
            return Kind::Presence;
        }

        if (length >= 18 && data[1] == 0x01)
        {
            return Kind::Input;
        }

        if (length >= 13 && data[0] == 0x00 && data[1] == 0x0f &&
            data[2] == 0x00 && data[3] == 0xf0)
        {
            return Kind::Link;
        }

        return Kind::Other;
    }

    // For Kind::Presence: is a controller there?
    inline bool present(const uint8_t *data)
    {
        return (data[1] & 0x80) != 0;
    }

    /*
     * For Kind::Link: bytes 5-12, which stayed the same for one controller
     * across every connection and every run of the probe. They tell two
     * controllers apart and let macOS recognise one again, as the radio
     * address does for the Xbox One pads.
     */
    inline std::string identity(const uint8_t *data, size_t length)
    {
        static const char digits[] = "0123456789abcdef";
        std::string text;

        for (size_t i = 5; i < 13 && i < length; i++)
        {
            text += digits[data[i] >> 4];
            text += digits[data[i] & 0x0f];
        }

        return text;
    }

    // Triggers arrive as 0..255; GamepadState keeps the Xbox One's 0..1023.
    inline uint16_t triggerFrom(uint8_t value)
    {
        return static_cast<uint16_t>((value * 1023 + 127) / 255);
    }

    inline int16_t stickFrom(const uint8_t *bytes)
    {
        return static_cast<int16_t>(bytes[0] | (bytes[1] << 8));
    }

    /*
     * For Kind::Input. The pad starts at byte 4:
     *
     *   6      d-pad up 01, down 02, left 04, right 08, Start 10, Back 20,
     *          left stick click 40, right stick click 80
     *   7      LB 01, RB 02, Xbox button 04, A 10, B 20, X 40, Y 80
     *   8, 9   left and right trigger, 0..255
     *   10-17  left X, left Y, right X, right Y: int16, little-endian, +Y up
     *
     * +Y up is also GamepadState's convention, so nothing is inverted here.
     */
    inline bool parseInput(const uint8_t *data, size_t length, GamepadState &state)
    {
        if (classify(data, length) != Kind::Input)
        {
            return false;
        }

        const uint8_t b6 = data[6], b7 = data[7];

        state.dpadUp    = (b6 & 0x01) != 0;
        state.dpadDown  = (b6 & 0x02) != 0;
        state.dpadLeft  = (b6 & 0x04) != 0;
        state.dpadRight = (b6 & 0x08) != 0;
        state.start     = (b6 & 0x10) != 0;
        state.select    = (b6 & 0x20) != 0;
        state.thumbLeft  = (b6 & 0x40) != 0;
        state.thumbRight = (b6 & 0x80) != 0;

        state.bumperLeft  = (b7 & 0x01) != 0;
        state.bumperRight = (b7 & 0x02) != 0;
        state.guide       = (b7 & 0x04) != 0;
        state.a = (b7 & 0x10) != 0;
        state.b = (b7 & 0x20) != 0;
        state.x = (b7 & 0x40) != 0;
        state.y = (b7 & 0x80) != 0;

        state.triggerLeft  = triggerFrom(data[8]);
        state.triggerRight = triggerFrom(data[9]);

        state.stickLeftX  = stickFrom(data + 10);
        state.stickLeftY  = stickFrom(data + 12);
        state.stickRightX = stickFrom(data + 14);
        state.stickRightY = stickFrom(data + 16);

        return true;
    }

    // "Is anybody there?" Every slot answers with a presence packet.
    inline Command presenceQuery()
    {
        return Command{ 0x08, 0x00, 0x0f, 0xc0 };
    }

    /*
     * The ring. xpad's numbering: 2..5 blink, then light quadrant 1..4 —
     * which is what a console does, and what stops the ring from spinning.
     * `slot` counts from 0.
     */
    inline Command ringLight(int slot)
    {
        return Command{ 0x00, 0x00, 0x08,
                        static_cast<uint8_t>(0x40 + 2 + (slot & 0x03)) };
    }

    // Motors 0..255; the 360 pad has no trigger motors.
    inline Command rumble(uint8_t strong, uint8_t weak)
    {
        return Command{ 0x00, 0x01, 0x0f, 0xc0, 0x00, strong, weak };
    }

    inline Command powerOff()
    {
        return Command{ 0x00, 0x00, 0x08, 0xc0 };
    }

    // RumbleEffect magnitudes are 0..100 (the Xbox One's scale).
    inline uint8_t motorFrom(uint8_t percent)
    {
        if (percent >= 100)
        {
            return 255;
        }

        return static_cast<uint8_t>((percent * 255 + 50) / 100);
    }
}
