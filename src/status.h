/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A small thread-safe status board shared between the driver and the menu bar.
 *
 * The driver writes from its USB thread; the menu bar reads from the main
 * thread once a second. Polling rather than pushing keeps this free of
 * cross-thread callbacks and the lifetime questions they bring — a status line
 * does not need to be more immediate than that.
 */

#pragma once

#include "status_c.h"

#include <string>

namespace Status
{
    // A line about the app as a whole: "Waiting for adapter", a refusal.
    // Empty once there is nothing to say beyond the controllers themselves.
    void setMessage(const std::string &text);

    // Controllers go by the number the adapter gave them, from 1. It stays
    // the same while a controller is connected, so the menu does not
    // renumber the others when one of them leaves.
    void controllerConnected(int number);
    void controllerDisconnected(int number);
    void setBattery(int number, const std::string &level);
}
