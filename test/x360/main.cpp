/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The Xbox 360 receiver's packets, held against what a real one sent.
 *
 * Every packet below was recorded with tools/x360-probe.c from a receiver
 * (045e:0719) and a controller on a Mac, one input at a time, and the
 * expectation beside it is what was pressed. The layout comes from Linux's
 * xpad; this is what makes it more than a copy of a claim.
 *
 *   clang++ -std=c++11 -I src test/x360/main.cpp -o /tmp/x360t && /tmp/x360t
 */

#include "x360/protocol.h"

#include <cstdio>
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

    using Packet = std::vector<uint8_t>;

    // The recorded input packets share everything but bytes 6-17.
    Packet input(uint8_t b6, uint8_t b7, uint8_t lt = 0, uint8_t rt = 0,
                 std::vector<uint8_t> sticks = { 0x1a, 0xf2, 0x40, 0x17,
                                                 0x3a, 0xfd, 0x17, 0x05 })
    {
        Packet p = { 0x00, 0x01, 0x00, 0xf0, 0x00, 0x13, b6, b7, lt, rt };
        p.insert(p.end(), sticks.begin(), sticks.end());
        p.resize(29, 0x00);
        return p;
    }

    GamepadState parse(const Packet &p)
    {
        GamepadState s;
        X360::parseInput(p.data(), p.size(), s);
        return s;
    }

    // Which buttons a state has down, by name, in a fixed order.
    std::string pressed(const GamepadState &s)
    {
        std::string out;
        auto add = [&out](bool on, const char *name) {
            if (on) out += (out.empty() ? "" : "+") + std::string(name);
        };

        add(s.a, "A"); add(s.b, "B"); add(s.x, "X"); add(s.y, "Y");
        add(s.bumperLeft, "LB"); add(s.bumperRight, "RB");
        add(s.select, "Back"); add(s.start, "Start"); add(s.guide, "Xbox");
        add(s.thumbLeft, "LS"); add(s.thumbRight, "RS");
        add(s.dpadUp, "Up"); add(s.dpadDown, "Down");
        add(s.dpadLeft, "Left"); add(s.dpadRight, "Right");

        return out.empty() ? "nothing" : out;
    }
}

int main()
{
    printf("buttons, as recorded:\n");

    struct Recorded { uint8_t b6, b7; const char *expected; };

    const Recorded buttons[] = {
        { 0x00, 0x10, "A" },
        { 0x00, 0x20, "B" },
        { 0x00, 0x80, "Y" },
        { 0x00, 0x40, "X" },
        { 0x10, 0x00, "Start" },
        { 0x20, 0x00, "Back" },
        { 0x00, 0x01, "LB" },
        { 0x00, 0x02, "RB" },
        { 0x40, 0x00, "LS" },
        { 0x80, 0x00, "RS" },
        { 0x00, 0x04, "Xbox" },
        { 0x01, 0x00, "Up" },
        { 0x02, 0x00, "Down" },
        { 0x04, 0x00, "Left" },
        { 0x08, 0x00, "Right" },
        { 0x0a, 0x00, "Down+Right" },   // a diagonal, as it came in
        { 0x00, 0x00, "nothing" },
    };

    for (const Recorded &r : buttons)
    {
        const std::string got = pressed(parse(input(r.b6, r.b7)));

        expect(got == r.expected, std::string(r.expected) + ": read as " + got);
    }

    printf("\ntriggers and sticks:\n");

    expect(parse(input(0, 0, 0xff, 0)).triggerLeft == 1023,
           "left trigger fully pressed (ff) is 1023, the adapter's full scale");
    expect(parse(input(0, 0, 0, 0xff)).triggerRight == 1023,
           "right trigger fully pressed is 1023");
    expect(parse(input(0, 0, 0x5d, 0)).triggerLeft == (0x5d * 1023 + 127) / 255,
           "a trigger half way scales in proportion");

    const GamepadState rest = parse(input(0, 0));

    expect(rest.stickLeftX == static_cast<int16_t>(0xf21a) &&
           rest.stickLeftY == 0x1740 &&
           rest.stickRightX == static_cast<int16_t>(0xfd3a) &&
           rest.stickRightY == 0x0517,
           "sticks: four little-endian int16 at bytes 10-17 (this pad drifts)");

    printf("\nthe other packets:\n");

    const Packet connected = { 0x08, 0x80 };
    const Packet empty = { 0x08, 0x00 };
    const Packet link = { 0x00, 0x0f, 0x00, 0xf0, 0x00, 0xcc, 0xe0, 0x23, 0xf2,
                          0x50, 0x69, 0x7f, 0xf7, 0x60, 0x00, 0x20, 0x13, 0xe3,
                          0x20, 0x1d, 0x30, 0x03, 0x40, 0x01, 0x50, 0x01, 0xff,
                          0xff, 0xff };
    Packet idle = { 0x00, 0x00, 0x00, 0xf0 };
    idle.resize(29, 0x00);
    Packet upkeep = { 0x00, 0xf8, 0x02, 0x00 };
    upkeep.resize(29, 0x00);

    expect(X360::classify(connected.data(), connected.size()) == X360::Kind::Presence &&
           X360::present(connected.data()),
           "08 80: a controller connected");
    expect(X360::classify(empty.data(), empty.size()) == X360::Kind::Presence &&
           !X360::present(empty.data()),
           "08 00: the slot is empty");
    expect(X360::classify(link.data(), link.size()) == X360::Kind::Link &&
           X360::identity(link.data(), link.size()) == "cce023f250697ff7",
           "00 0f 00 f0: the link packet, identity cce023f250697ff7 (the same in every run)");
    expect(X360::classify(idle.data(), idle.size()) == X360::Kind::Other &&
           X360::classify(upkeep.data(), upkeep.size()) == X360::Kind::Other,
           "00 00 00 f0 and 00 f8 0x 00: upkeep, not input");

    GamepadState untouched;
    untouched.a = true;

    expect(!X360::parseInput(link.data(), link.size(), untouched) && untouched.a,
           "a packet that is not input leaves the state alone");

    printf("\ncommands (the ring lit and the motors ran with these):\n");

    const X360::Command led = X360::ringLight(0);
    const X360::Command motors = X360::rumble(0xff, 0x00);

    expect(led[0] == 0x00 && led[1] == 0x00 && led[2] == 0x08 && led[3] == 0x42,
           "ring for slot 1: 00 00 08 42");
    expect(X360::ringLight(3)[3] == 0x45, "ring for slot 4: 00 00 08 45");
    expect(motors[1] == 0x01 && motors[2] == 0x0f && motors[3] == 0xc0 &&
           motors[5] == 0xff && motors[6] == 0x00,
           "rumble: 00 01 0f c0 00 strong weak");
    expect(X360::presenceQuery()[0] == 0x08 && X360::presenceQuery()[3] == 0xc0,
           "presence query: 08 00 0f c0");
    expect(X360::powerOff()[2] == 0x08 && X360::powerOff()[3] == 0xc0,
           "power off: 00 00 08 c0");

    expect(X360::motorFrom(0) == 0 && X360::motorFrom(100) == 255 &&
           X360::motorFrom(50) == 128 && X360::motorFrom(150) == 255,
           "motor strength: 0..100 becomes 0..255");

    printf(failures ? "\n%d failed\n" : "\nall passed\n", failures);

    return failures ? 1 : 0;
}
