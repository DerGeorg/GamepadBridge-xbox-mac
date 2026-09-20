/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "permissions.h"

#include <IOKit/hidsystem/IOHIDLib.h>

namespace
{
    Permissions::Access translate(IOHIDAccessType type)
    {
        switch (type)
        {
            case kIOHIDAccessTypeGranted: return Permissions::Access::Granted;
            case kIOHIDAccessTypeDenied:  return Permissions::Access::Denied;
            default:                      return Permissions::Access::Unknown;
        }
    }
}

Permissions::Access Permissions::inputMonitoring()
{
    return translate(IOHIDCheckAccess(kIOHIDRequestTypeListenEvent));
}

Permissions::Access Permissions::requestInputMonitoring()
{
    // Only prompts while the answer is Unknown; once denied, macOS will not
    // ask again and the user has to change it in System Settings.
    IOHIDRequestAccess(kIOHIDRequestTypeListenEvent);

    return inputMonitoring();
}

const char *Permissions::settingsURL()
{
    return "x-apple.systempreferences:com.apple.preference.security"
           "?Privacy_ListenEvent";
}
