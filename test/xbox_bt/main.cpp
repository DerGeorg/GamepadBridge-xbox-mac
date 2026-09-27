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
 * And the way back: a rumble report from SDL has to leave for the controller
 * as exactly the command SDL itself sends a wired Xbox One pad.
 *
 *   clang++ -std=c++11 -I src test/xbox_bt/main.cpp -o /tmp/xbt && /tmp/xbt
 */

#include "xbox_bt_profile.h"
#include "controller/controller.h"

#include <cstdio>
#include <cstring>
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

    /*
     * Walks a report descriptor the way the OS does and adds up the bits
     * each report carries. A descriptor mistake is not an error anywhere
     * else: macOS just drops the reports, and the pad looks dead.
     */
    struct Layout
    {
        bool balanced = false;           // every Collection is closed
        int  topLevel = 0;               // top-level collections
        std::map<int, int> inputBits;    // report ID -> bits
        std::map<int, int> outputBits;
    };

    Layout walk(const std::vector<uint8_t> &d)
    {
        Layout layout;
        int depth = 0, reportId = 0, size = 0, count = 0;

        for (size_t i = 0; i < d.size();)
        {
            const uint8_t prefix = d[i];
            const int length = (prefix & 3) == 3 ? 4 : (prefix & 3);

            uint32_t value = 0;

            for (int k = 0; k < length && i + 1 + k < d.size(); k++)
            {
                value |= uint32_t(d[i + 1 + k]) << (8 * k);
            }

            switch (prefix & 0xFC)
            {
                case 0x84: reportId = int(value); break;           // Report ID
                case 0x74: size = int(value); break;               // Report Size
                case 0x94: count = int(value); break;              // Report Count
                case 0x80: layout.inputBits[reportId] += size * count; break;
                case 0x90: layout.outputBits[reportId] += size * count; break;
                case 0xA0: if (depth++ == 0) layout.topLevel++; break;
                case 0xC0: depth--; break;
            }

            i += 1 + length;
        }

        layout.balanced = depth == 0;

        return layout;
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

    /*
     * The descriptor as published, and the plain dump it is built from —
     * both from makeXboxBtDescriptor(), which output_corehid.cpp calls too.
     */
    printf("\ndescriptor:\n");

    const std::vector<uint8_t> base = makeXboxBtDescriptor(false);
    const std::vector<uint8_t> guided = makeXboxBtDescriptor();

    expect(base.size() == sizeof(kXboxBtReportDescriptor)
           && guided.size() == base.size() + sizeof(kXboxBtGuideItems),
           "published = the dump plus the Xbox button's items, nothing else");

    Layout plain = walk(base), withGuide = walk(guided);

    expect(plain.balanced && plain.topLevel == 1,
           "one game pad collection, every collection closed");
    expect(plain.inputBits[1] == 8 * (int(sizeof(XboxBtReport)) - 1),
           "report 1 declares exactly the 15 bytes after its ID");
    expect(plain.outputBits[3] == 8 * 8,
           "report 3 (rumble) declares the 8 bytes parseXboxBtRumble reads");
    expect(withGuide.balanced && withGuide.topLevel == 1,
           "as published: still one collection, still closed");
    expect(withGuide.inputBits[2] == 8 * (int(sizeof(XboxBtGuideReport)) - 1)
           && withGuide.inputBits[1] == plain.inputBits[1]
           && withGuide.outputBits[3] == plain.outputBits[3],
           "report 2 is one byte, reports 1 and 3 unchanged");

    /*
     * Rumble. SDL_hidapi_xboxone.c sends a Bluetooth pad
     *     03 0F LT RT L R FF 00 EB
     * and a wired one the GIP command 09 00 seq 09 followed by
     *     00 0F LT RT L R FF 00 EB
     * The payloads differ only in their first byte, which is why a request
     * can pass through untouched.
     */
    printf("\nrumble — what SDL sends, and what leaves for the controller:\n");

    const uint8_t sdlBluetooth[] = { 0x03, 0x0F, 20, 40, 60, 80, 0xFF, 0x00, 0xEB };
    const uint8_t sdlWiredPayload[] = { 0x00, 0x0F, 20, 40, 60, 80, 0xFF, 0x00, 0xEB };

    RumbleEffect effect;

    expect(parseXboxBtRumble(0, sdlBluetooth, sizeof(sdlBluetooth), effect),
           "SDL's report is accepted with the ID in front");

    const auto gip = Controller::gipRumble(effect);

    expect(sizeof(gip) == sizeof(sdlWiredPayload)
           && memcmp(&gip, sdlWiredPayload, sizeof(gip)) == 0,
           "and leaves as the bytes SDL sends a wired controller");

    RumbleEffect beside;

    expect(parseXboxBtRumble(0x03, sdlBluetooth + 1, sizeof(sdlBluetooth) - 1,
                             beside)
           && memcmp(&beside, &effect, sizeof(effect)) == 0,
           "the same with the ID given beside the data");

    const uint8_t stop[] = { 0x03, 0x0F, 0, 0, 0, 0, 0xFF, 0x00, 0xEB };
    RumbleEffect stopped;

    expect(parseXboxBtRumble(0, stop, sizeof(stop), stopped)
           && stopped.left == 0 && stopped.right == 0
           && stopped.leftTrigger == 0 && stopped.rightTrigger == 0,
           "a stop is all zero magnitudes");

    const uint8_t loud[] = { 0x03, 0x0F, 255, 101, 200, 100, 0xFF, 0x00, 0xEB };
    RumbleEffect capped;

    expect(parseXboxBtRumble(0, loud, sizeof(loud), capped)
           && capped.leftTrigger == 100 && capped.rightTrigger == 100
           && capped.left == 100 && capped.right == 100,
           "magnitudes above 100 are capped at 100");

    const uint8_t otherId[] = { 0x05, 0x0F, 20, 40, 60, 80, 0xFF, 0x00, 0xEB };
    const uint8_t tooShort[] = { 0x03, 0x0F, 20, 40 };
    RumbleEffect unused;

    expect(!parseXboxBtRumble(0, otherId, sizeof(otherId), unused)
           && !parseXboxBtRumble(0x05, otherId + 1, 8, unused)
           && !parseXboxBtRumble(0, tooShort, sizeof(tooShort), unused),
           "other reports and short ones are refused");

    printf(failures ? "\n%d failed\n" : "\nall passed\n", failures);

    return failures ? 1 : 0;
}
