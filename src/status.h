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
    void setConnection(const std::string &text, bool controllerPresent);
    void setBattery(const std::string &text);
}
