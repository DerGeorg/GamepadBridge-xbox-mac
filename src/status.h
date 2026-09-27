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
    // A line about the app as a whole: "Needs permissions", a refusal.
    // Empty once there is nothing to say beyond the controllers themselves.
    void setMessage(const std::string &text);

    // A line from one source that is not a controller - the Xbox 360
    // receiver being held by another app, say. Empty text removes it.
    void setNotice(const std::string &source, const std::string &text);

    /*
     * Controllers by a key that stays the same while they are connected, so
     * the menu does not renumber the others when one leaves: the adapter's
     * number (1-16) for Xbox One pads, 100 + slot for Xbox 360 pads. The menu
     * lists them in key order, under their label.
     */
    void controllerConnected(int key, const std::string &label);
    void controllerDisconnected(int key);
    void setBattery(int key, const std::string &level);

    // Which receivers are there, for the menu's pairing entries: the
    // adapter pairs from the menu, the Xbox 360 receiver only by its button.
    void setAdapterPresent(bool present);
    void setReceiverPresent(bool present);
}
