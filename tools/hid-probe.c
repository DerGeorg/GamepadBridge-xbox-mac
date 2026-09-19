/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * hid-probe — live values straight from the raw HID path.
 *
 * macOS routes controllers two ways: raw HID (IOKit, used by SDL games,
 * emulators and browsers) and GameController.framework (System Settings and
 * Apple-native ports). They fail independently, so when a game sees nothing
 * it matters which of the two is silent. Run this next to `gc-probe watch`:
 *
 *   this one moves, gc-probe does not  -> the pad works, GameController is
 *                                         filtering or mismapping it
 *   neither moves                      -> no reports are reaching the OS
 *
 * Matches any game pad (Generic Desktop / Game Pad) so it works whatever
 * GAMEPADBRIDGE_IDS is set to.
 *
 * Build and run:
 *   clang -framework IOKit -framework CoreFoundation \
 *         -o /tmp/hid-probe tools/hid-probe.c && /tmp/hid-probe
 */

#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>

static const char *usageName(uint32_t page, uint32_t usage)
{
    if (page == kHIDPage_Button)
    {
        return "Button";
    }

    if (page != kHIDPage_GenericDesktop)
    {
        return "?";
    }

    switch (usage)
    {
        case kHIDUsage_GD_X:        return "X  (left stick X)";
        case kHIDUsage_GD_Y:        return "Y  (left stick Y)";
        case kHIDUsage_GD_Z:        return "Z  (left trigger)";
        case kHIDUsage_GD_Rx:       return "Rx (right stick X)";
        case kHIDUsage_GD_Ry:       return "Ry (right stick Y)";
        case kHIDUsage_GD_Rz:       return "Rz (right trigger)";
        case kHIDUsage_GD_Hatswitch:return "Hat (d-pad)";
        default:                    return "?";
    }
}

static void onValue(void *ctx, IOReturn result, void *sender, IOHIDValueRef v)
{
    (void)ctx; (void)result; (void)sender;

    IOHIDElementRef e = IOHIDValueGetElement(v);

    const uint32_t page  = IOHIDElementGetUsagePage(e);
    const uint32_t usage = IOHIDElementGetUsage(e);

    // Ignore the constant padding bits declared at the end of the report.
    if (page != kHIDPage_Button && page != kHIDPage_GenericDesktop)
    {
        return;
    }

    if (page == kHIDPage_Button)
    {
        printf("Button %-2u  %s\n",
               usage, IOHIDValueGetIntegerValue(v) ? "DOWN" : "up");
    }
    else
    {
        printf("%-22s %ld\n",
               usageName(page, usage), (long)IOHIDValueGetIntegerValue(v));
    }

    fflush(stdout);
}

static void onMatch(void *ctx, IOReturn result, void *sender, IOHIDDeviceRef d)
{
    (void)ctx; (void)result; (void)sender;

    char product[256] = "(unnamed)";
    CFStringRef name = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDProductKey));

    if (name)
    {
        CFStringGetCString(name, product, sizeof(product), kCFStringEncodingUTF8);
    }

    int vendor = 0, model = 0;
    CFNumberRef n;

    if ((n = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDVendorIDKey))))
    {
        CFNumberGetValue(n, kCFNumberIntType, &vendor);
    }

    if ((n = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDProductIDKey))))
    {
        CFNumberGetValue(n, kCFNumberIntType, &model);
    }

    char transport[64] = "(none)";
    CFStringRef link = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDTransportKey));

    if (link)
    {
        CFStringGetCString(link, transport, sizeof(transport),
                           kCFStringEncodingUTF8);
    }

    printf("game pad: %s  %04x:%04x  transport=%s\n",
           product, vendor, model, transport);
    printf("press buttons and move the sticks:\n");
    fflush(stdout);
}

int main(void)
{
    IOHIDManagerRef manager =
        IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

    int page = kHIDPage_GenericDesktop;
    int usage = kHIDUsage_GD_GamePad;

    CFNumberRef pageRef  = CFNumberCreate(NULL, kCFNumberIntType, &page);
    CFNumberRef usageRef = CFNumberCreate(NULL, kCFNumberIntType, &usage);

    const void *keys[] = {
        CFSTR(kIOHIDDeviceUsagePageKey), CFSTR(kIOHIDDeviceUsageKey)
    };
    const void *values[] = { pageRef, usageRef };

    CFDictionaryRef match = CFDictionaryCreate(
        NULL, keys, values, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    IOHIDManagerSetDeviceMatching(manager, match);
    IOHIDManagerRegisterDeviceMatchingCallback(manager, onMatch, NULL);
    IOHIDManagerRegisterInputValueCallback(manager, onValue, NULL);
    IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(),
                                    kCFRunLoopDefaultMode);

    IOReturn rc = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);

    if (rc != kIOReturnSuccess)
    {
        printf("IOHIDManagerOpen failed: 0x%x "
               "(grant this terminal Input Monitoring)\n", rc);

        return 1;
    }

    printf("waiting for a game pad...\n");
    fflush(stdout);

    CFRunLoopRun();

    return 0;
}
