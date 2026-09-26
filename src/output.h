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

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

/*
 * macOS port note
 * ----------------
 * On Linux, xow exposes the controller through the kernel's `uinput`
 * subsystem (see the original controller/input.{h,cpp}). macOS has no
 * uinput, so this header defines a thin, OS-neutral output abstraction.
 *
 * The dongle/protocol layer fills a normalized GamepadState and hands it
 * to an OutputDevice backend. Backends are interchangeable:
 *
 *   - ConsoleOutput   (output_console.cpp)  -> Stage 1: prints input,
 *                                              proves the dongle works.
 *   - DriverKitOutput (output_driverkit.cpp)-> Stage 2: feeds a virtual
 *                                              HID gamepad extension so
 *                                              games see a real controller.
 *
 * The CMake build links exactly one backend, which provides
 * makeOutputDevice().
 */

// Normalized gamepad state. Ranges match the values reported by the
// controller over GIP (see GipDevice::InputData in controller/gip.h).
struct GamepadState
{
    // Face / system buttons
    bool a = false, b = false, x = false, y = false;
    bool start = false, select = false, guide = false;

    // Shoulder buttons and stick clicks
    bool bumperLeft = false, bumperRight = false;
    bool thumbLeft = false, thumbRight = false;

    // D-pad
    bool dpadUp = false, dpadDown = false, dpadLeft = false, dpadRight = false;

    // Triggers: 0..1023 (10 bits, unsigned)
    uint16_t triggerLeft = 0, triggerRight = 0;

    // Sticks: -32768..32767 (16 bits, signed). +Y = up.
    int16_t stickLeftX = 0, stickLeftY = 0;
    int16_t stickRightX = 0, stickRightY = 0;
};

/*
 * Force feedback from a game towards the controller.
 *
 * The same fields in the same order as the controller's own rumble command
 * (GipDevice::RumbleData) and as the Bluetooth pad's output report: Microsoft
 * tunnels one format through both, so a request passes through untouched.
 *
 *   enable     which motors this request sets: 0x01 right (weak), 0x02 left
 *              (strong), 0x04 right trigger, 0x08 left trigger
 *   magnitude  0..100 per motor
 *   duration   in 10 ms steps, then `delay` off, repeated `repeat` more times
 */
struct RumbleEffect
{
    uint8_t enable = 0;
    uint8_t leftTrigger = 0, rightTrigger = 0;
    uint8_t left = 0, right = 0;
    uint8_t duration = 0, delay = 0, repeat = 0;
};

// Identity reported by the controller during the GIP announce handshake.
struct DeviceInfo
{
    uint16_t vendorId = 0;
    uint16_t productId = 0;
    uint16_t version = 0;
    std::string name;

    // Tells two controllers of the same model apart. Games and SDL key
    // their per-controller settings on it, so it must stay the same across
    // reconnects: the controller's radio address, not a slot number.
    std::string serial;
};

/*
 * Output backend interface (replaces Linux InputDevice).
 */
class OutputDevice
{
public:
    using RumbleCallback = std::function<void(const RumbleEffect &)>;

    virtual ~OutputDevice() = default;

    // Called once when the controller has finished announcing itself.
    virtual void create(const DeviceInfo &info) = 0;

    // Called on every input report with the full current state.
    virtual void update(const GamepadState &state) = 0;

    // Optional: backend invokes this when the OS sends rumble to the
    // virtual device. Default backends that have no FF channel ignore it.
    // Set before create(): a game may send rumble the moment the device
    // appears. The callback runs on whichever thread the OS delivers on, so
    // it must not touch USB itself.
    virtual void setRumbleCallback(RumbleCallback) {}
};

// Implemented by whichever backend .cpp the build links.
std::unique_ptr<OutputDevice> makeOutputDevice();
