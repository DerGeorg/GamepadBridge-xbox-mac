/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Checks every button against BOTH readers of the Xbox Bluetooth report —
 * and checks that the model of those readers matches what was observed.
 *
 * Two readers, both reading fixed bit positions, neither using the button
 * usages in the descriptor:
 *
 *   apple()  GameController.framework: the button field read consecutively.
 *            Measured with tools/gc-probe.m.
 *   sdl()    SDL, which Wine/CrossOver and Steam Input build on. Picks its
 *            layout by report LENGTH — copied from SDL_hidapi_xboxone.c.
 *
 * An earlier version of this test modelled GameController as reading the
 * descriptor. That model was wrong, and a fix built on it passed every check
 * here while scrambling half the buttons on a real Mac. So the model is now
 * held to the record: the two historical layouts at the bottom must fail in
 * exactly the way that was seen, or the model is wrong again.
 *
 *   clang++ -std=c++11 -I src test/xbox_bt/main.cpp -o /tmp/xbt && /tmp/xbt
 */

#include "xbox_bt_profile.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void expect(bool ok, const std::string &what)
    {
        printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());

        if (!ok) failures++;
    }

    using Seen = std::vector<std::string>;

    std::string show(const Seen &seen)
    {
        if (seen.empty()) return "nothing";

        std::string out;

        for (const std::string &s : seen) out += (out.empty() ? "" : "+") + s;

        return out;
    }

    // GameController: bit N of the field at bytes 14-15 is the Nth button.
    Seen apple(const uint8_t *data)
    {
        static const char *names[] = {
            "A", "B", "X", "Y", "LB", "RB", "View", "Menu", "LS", "RS"
        };

        const unsigned field = data[14] | (data[15] << 8);
        Seen seen;

        for (int k = 0; k < 10; k++)
        {
            if (field & (1u << k)) seen.push_back(names[k]);
        }

        return seen;
    }

    // SDL, HIDAPI_DriverXboxOneBluetooth_HandleStatePacket: size == 16 takes
    // HandleButtons16, size > 16 takes HandleButtons. data[0] is the report ID.
    Seen sdl(const uint8_t *data, int size)
    {
        Seen seen;

        if (size == 16)
        {
            if (data[14] & 0x01) seen.push_back("A");
            if (data[14] & 0x02) seen.push_back("B");
            if (data[14] & 0x04) seen.push_back("X");
            if (data[14] & 0x08) seen.push_back("Y");
            if (data[14] & 0x10) seen.push_back("LB");
            if (data[14] & 0x20) seen.push_back("RB");
            if (data[14] & 0x40) seen.push_back("View");
            if (data[14] & 0x80) seen.push_back("Menu");
            if (data[15] & 0x01) seen.push_back("LS");
            if (data[15] & 0x02) seen.push_back("RS");
        }
        else if (size > 16)
        {
            if (data[14] & 0x01) seen.push_back("A");
            if (data[14] & 0x02) seen.push_back("B");
            if (data[14] & 0x08) seen.push_back("X");
            if (data[14] & 0x10) seen.push_back("Y");
            if (data[14] & 0x40) seen.push_back("LB");
            if (data[14] & 0x80) seen.push_back("RB");
            if (data[15] & 0x04) seen.push_back("View");
            if (data[15] & 0x08) seen.push_back("Menu");
            if (data[15] & 0x10) seen.push_back("Guide");
            if (data[15] & 0x20) seen.push_back("LS");
            if (data[15] & 0x40) seen.push_back("RS");
        }

        return seen;
    }

    struct Button { const char *name; bool GamepadState::*field; };

    const Button buttons[] = {
        { "A",    &GamepadState::a           },
        { "B",    &GamepadState::b           },
        { "X",    &GamepadState::x           },
        { "Y",    &GamepadState::y           },
        { "LB",   &GamepadState::bumperLeft  },
        { "RB",   &GamepadState::bumperRight },
        { "View", &GamepadState::select      },
        { "Menu", &GamepadState::start       },
        { "LS",   &GamepadState::thumbLeft   },
        { "RS",   &GamepadState::thumbRight  },
    };

    // A historical report: 17 bytes, one button pressed at the given bit.
    std::vector<uint8_t> legacy(int bit)
    {
        std::vector<uint8_t> data(17, 0);

        data[0] = 0x01;
        data[14 + bit / 8] = uint8_t(1u << (bit % 8));

        return data;
    }
}

int main()
{
    printf("the report as built today:\n");
    expect(sizeof(XboxBtReport) == 16,
           "16 bytes including the report ID, so SDL reads consecutively");

    for (const Button &b : buttons)
    {
        GamepadState state;
        state.*(b.field) = true;

        const XboxBtReport report = makeXboxBtReport(state);
        const uint8_t *data = reinterpret_cast<const uint8_t *>(&report);

        const Seen a = apple(data), s = sdl(data, int(sizeof(report)));

        expect(show(a) == b.name && show(s) == b.name,
               std::string(b.name) + ": GameController sees " + show(a)
               + ", SDL sees " + show(s));
    }

    /*
     * The record. Each row is what a real Mac showed for that historical
     * layout; the model has to reproduce it, or it is not a model of anything.
     */
    struct Observed { const char *pressed; int bit; const char *seen; };

    printf("\nmodel check — 1.0.0 to 1.0.6 (17 bytes, consecutive), "
           "PlateUp! under CrossOver:\n");

    const Observed plateup[] = {
        { "Y",    3, "X"  },   // "cannot press Y"
        { "Menu", 7, "RB" },   // "cannot press pause"
    };

    for (const Observed &o : plateup)
    {
        const std::vector<uint8_t> data = legacy(o.bit);
        const std::string seen = show(sdl(data.data(), 17));

        expect(seen == o.seen, std::string("SDL reads ") + o.pressed + " as "
               + seen + ", as observed: " + o.seen);
    }

    printf("\nmodel check — the gapped attempt (17 bytes), "
           "gc-probe on the Mac:\n");

    const Observed gcprobe[] = {
        { "X",    3,  "Y"       },
        { "Y",    4,  "LB"      },
        { "LB",   6,  "View"    },
        { "RB",   7,  "Menu"    },
        { "View", 10, "nothing" },
        { "Menu", 11, "nothing" },
        { "LS",   13, "nothing" },
        { "RS",   14, "nothing" },
    };

    for (const Observed &o : gcprobe)
    {
        const std::vector<uint8_t> data = legacy(o.bit);
        const std::string seen = show(apple(data.data()));

        expect(seen == o.seen, std::string("GameController reads ") + o.pressed
               + " as " + seen + ", as observed: " + o.seen);
    }

    printf(failures ? "\n%d failed\n" : "\nall passed\n", failures);

    return failures ? 1 : 0;
}
