/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Input Monitoring, which macOS requires before a virtual HID device can be
 * published.
 *
 * Without it CoreHID simply returns nil. No prompt, no error, nothing in the
 * log — the app looks like it started fine and the controller does nothing.
 * So the permission is checked up front and asked for explicitly, rather than
 * being discovered as a failure several steps later.
 */

#pragma once

namespace Permissions
{
    enum class Access
    {
        Granted,
        Denied,   // the user said no, or revoked it
        Unknown,  // never asked
    };

    Access inputMonitoring();

    // Shows the system prompt when the answer is still Unknown. Returns the
    // state afterwards; granting usually only takes effect after a restart,
    // which macOS offers to do itself.
    Access requestInputMonitoring();

    // Deep link to the exact System Settings pane, so nobody has to hunt for
    // it among a dozen similarly named lists.
    const char *settingsURL();
}
