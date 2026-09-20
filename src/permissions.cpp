/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "permissions.h"

#include <IOKit/hid/IOHIDManager.h>
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

    /*
     * Opening a HID manager as well, because IOHIDRequestAccess on its own
     * does not reliably put the app into the Input Monitoring list — and an
     * app that is not listed cannot be switched on. The user would have to
     * find the binary through the + button, which is not something to ask of
     * anyone.
     *
     * IOHIDLib.h is explicit that this is the other path to the same request:
     * "the request will be made on the process's behalf in
     * IOHIDManagerOpen/IOHIDDeviceOpen calls". It is expected to fail while
     * access is missing; registering the app is the point.
     */
    IOHIDManagerRef manager =
        IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

    if (manager)
    {
        IOHIDManagerSetDeviceMatching(manager, nullptr);
        IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
        IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);

        CFRelease(manager);
    }

    return inputMonitoring();
}

const char *Permissions::settingsURL()
{
    return "x-apple.systempreferences:com.apple.preference.security"
           "?Privacy_ListenEvent";
}
