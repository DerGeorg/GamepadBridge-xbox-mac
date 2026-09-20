/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "permissions.h"
#include "status_c.h"

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

        /*
         * Scheduling on a run loop and then turning it is the part that
         * matters. IOHIDManagerOpen on its own returns without touching a
         * device; the devices are only opened as the run loop runs, and it is
         * that attempt which raises the request. An earlier version skipped
         * this and registered nothing at all.
         */
        IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(),
                                        kCFRunLoopDefaultMode);

        IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);

        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false);

        IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
        IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(),
                                          kCFRunLoopDefaultMode);

        CFRelease(manager);
    }

    return inputMonitoring();
}

const char *Permissions::settingsURL()
{
    return "x-apple.systempreferences:com.apple.preference.security"
           "?Privacy_ListenEvent";
}

const char *gpb_permission_message(void)
{
    return "GamepadBridge needs Input Monitoring to publish the virtual "
           "gamepad that games and System Settings see. Without it the "
           "controller connects but nothing receives its input.\n\n"
           "1. Open System Settings below\n"
           "2. Switch GamepadBridge on under Input Monitoring\n\n"
           "Not in the list? Use \"Show Me the App\", then drag "
           "GamepadBridge from the Finder window onto the list.\n\n"
           "This window stays open and notices by itself once you are done.";
}

const char *gpb_permission_settings_url(void)
{
    return Permissions::settingsURL();
}
