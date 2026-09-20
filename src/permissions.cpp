/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "permissions.h"
#include "status_c.h"

#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hidsystem/IOHIDLib.h>

#include <string>

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

    std::string message;
}

Permissions::Access Permissions::inputMonitoring()
{
    return translate(IOHIDCheckAccess(kIOHIDRequestTypeListenEvent));
}

bool Permissions::accessibility()
{
    return AXIsProcessTrusted();
}

bool Permissions::allGranted()
{
    return inputMonitoring() == Access::Granted && accessibility();
}

void Permissions::request()
{
    IOHIDRequestAccess(kIOHIDRequestTypeListenEvent);

    /*
     * Scheduling on a run loop and then turning it is the part that matters.
     * IOHIDManagerOpen on its own touches no device; the devices are opened as
     * the run loop runs, and it is that attempt which raises the request. An
     * earlier version skipped this and registered nothing at all.
     */
    IOHIDManagerRef manager =
        IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

    if (manager)
    {
        IOHIDManagerSetDeviceMatching(manager, nullptr);
        IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(),
                                        kCFRunLoopDefaultMode);
        IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);

        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false);

        IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
        IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(),
                                          kCFRunLoopDefaultMode);
        CFRelease(manager);
    }

    // Prompting here is what puts the app into the Accessibility list; macOS
    // shows its own dialog offering to open the settings.
    if (!accessibility())
    {
        const void *keys[] = { kAXTrustedCheckOptionPrompt };
        const void *values[] = { kCFBooleanTrue };

        CFDictionaryRef options = CFDictionaryCreate(
            kCFAllocatorDefault, keys, values, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

        AXIsProcessTrustedWithOptions(options);

        CFRelease(options);
    }
}

const char *Permissions::inputMonitoringURL()
{
    return "x-apple.systempreferences:com.apple.preference.security"
           "?Privacy_ListenEvent";
}

const char *Permissions::accessibilityURL()
{
    return "x-apple.systempreferences:com.apple.preference.security"
           "?Privacy_Accessibility";
}

// ---------------------------------------------------------------------------
// C ABI
// ---------------------------------------------------------------------------

int gpb_permission_granted(void)
{
    return Permissions::allGranted() ? 1 : 0;
}

/*
 * Rebuilt on each call so the window can show what is still outstanding
 * rather than a fixed list the user has to check off themselves.
 */
const char *gpb_permission_message(void)
{
    const bool input =
        Permissions::inputMonitoring() == Permissions::Access::Granted;
    const bool access = Permissions::accessibility();

    message =
        "GamepadBridge needs two permissions to publish the virtual gamepad "
        "that games see. Without them the controller connects but nothing "
        "receives its input.\n\n";

    message += input ? "✅  Input Monitoring\n"
                     : "—  Input Monitoring — still needed\n";

    message += access ? "✅  Accessibility\n"
                      : "—  Accessibility — still needed\n";

    message +=
        "\nmacOS calls the second one \"Gerätesteuerung und Datenzugriff\" in "
        "German, which matches nothing in the list — it is Accessibility.\n\n"
        "Not listed? Use \"Show Me the App\" and drag GamepadBridge onto the "
        "list. This window notices by itself once you are done.";

    return message.c_str();
}

const char *gpb_permission_input_url(void)
{
    return Permissions::inputMonitoringURL();
}

const char *gpb_permission_accessibility_url(void)
{
    return Permissions::accessibilityURL();
}
