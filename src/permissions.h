/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The two permissions macOS requires before a virtual HID device can be
 * published, neither of which it will tell you about:
 *
 *   Input Monitoring (kTCCServiceListenEvent)   - IOHIDCheckAccess
 *   Accessibility    (kTCCServiceAccessibility) - AXIsProcessTrusted
 *
 * Without them CoreHID returns nil. No prompt, no error, nothing in the log.
 * The app looks like it started fine and the controller does nothing.
 *
 * Accessibility is the one nobody finds: macOS announces it as "Gerätesteuerung
 * und Datenzugriff" in German, which matches nothing in the settings list.
 */

#pragma once

#include "status_c.h"

namespace Permissions
{
    enum class Access
    {
        Granted,
        Denied,   // the user said no, or revoked it
        Unknown,  // never asked
    };

    Access inputMonitoring();
    bool accessibility();

    bool allGranted();

    // Both show the system prompt where macOS still offers one, and register
    // the app so it appears in the relevant list at all.
    void request();

    const char *inputMonitoringURL();
    const char *accessibilityURL();
}
