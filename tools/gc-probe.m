/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * gc-probe — what does macOS make of this controller?
 *
 * A HID device can be perfectly healthy (visible in `hidutil list`, bound to
 * AppleUserHIDEventDriver) and still be invisible to GameController.framework,
 * which is what System Settings > Game Controllers and most Mac games use.
 * And once it *is* visible, the next question is whether the buttons land
 * where they should — macOS maps a device it recognises by vendor/product ID
 * onto a fixed Xbox layout, so a report byte in the wrong place shows up as
 * the wrong button rather than as an error.
 *
 * Build and run:
 *   clang -fobjc-arc -framework Foundation -framework GameController \
 *         -o /tmp/gc-probe tools/gc-probe.m && /tmp/gc-probe
 *
 *   /tmp/gc-probe watch     live element values; press buttons and read off
 *                           which GameController input each one drives
 */

#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

// Every event carries the time it arrived: a self-test step that maps to an
// unused control emits nothing at all, so matching a sweep by counting lines
// silently goes wrong. Timestamps line up with the driver's own log.
static void stamp(void)
{
    NSDateFormatter *f = [[NSDateFormatter alloc] init];
    f.dateFormat = @"HH:mm:ss";

    printf("%s  ", [f stringFromDate:[NSDate date]].UTF8String);
}

static void describe(GCController *c)
{
    printf("  - vendor=%s category=%s extendedGamepad=%s\n",
           c.vendorName.UTF8String ?: "(null)",
           c.productCategory.UTF8String ?: "(null)",
           c.extendedGamepad ? "yes" : "no");
}

static void dump(void)
{
    NSArray<GCController *> *controllers = [GCController controllers];

    printf("GameController sees %lu controller(s)\n",
           (unsigned long)controllers.count);

    for (GCController *c in controllers)
    {
        describe(c);
    }
}

// Print the resting state. Anything obviously wrong here — a stick pinned to
// an extreme, a trigger stuck at 1.0 — means the report layout is misread
// before a single button has been touched.
static void snapshot(GCExtendedGamepad *g)
{
    printf("\nresting state:\n");
    printf("  leftStick   x=%+.3f y=%+.3f\n",
           g.leftThumbstick.xAxis.value, g.leftThumbstick.yAxis.value);
    printf("  rightStick  x=%+.3f y=%+.3f\n",
           g.rightThumbstick.xAxis.value, g.rightThumbstick.yAxis.value);
    printf("  triggers    L=%.3f R=%.3f\n",
           g.leftTrigger.value, g.rightTrigger.value);
    printf("  dpad        x=%+.3f y=%+.3f\n",
           g.dpad.xAxis.value, g.dpad.yAxis.value);
    printf("  buttons     A=%d B=%d X=%d Y=%d LB=%d RB=%d menu=%d options=%d\n",
           g.buttonA.isPressed, g.buttonB.isPressed,
           g.buttonX.isPressed, g.buttonY.isPressed,
           g.leftShoulder.isPressed, g.rightShoulder.isPressed,
           g.buttonMenu.isPressed, g.buttonOptions.isPressed);
    printf("\nnow press buttons one at a time:\n");
    fflush(stdout);
}

static void watch(GCController *c)
{
    GCExtendedGamepad *g = c.extendedGamepad;

    if (!g)
    {
        printf("  (no extendedGamepad profile — nothing to watch)\n");
        return;
    }

    snapshot(g);

    g.valueChangedHandler = ^(GCExtendedGamepad *pad, GCControllerElement *el) {
        const char *name = el.localizedName.UTF8String ?: "?";

        if ([el isKindOfClass:[GCControllerButtonInput class]])
        {
            GCControllerButtonInput *b = (GCControllerButtonInput *)el;

            stamp();
            printf("%-22s %-4s value=%.3f\n",
                   name, b.isPressed ? "DOWN" : "up", b.value);
        }
        else if ([el isKindOfClass:[GCControllerDirectionPad class]])
        {
            GCControllerDirectionPad *d = (GCControllerDirectionPad *)el;

            stamp();
            printf("%-22s x=%+.3f y=%+.3f\n",
                   name, d.xAxis.value, d.yAxis.value);
        }
        else if ([el isKindOfClass:[GCControllerAxisInput class]])
        {
            stamp();
            printf("%-22s %+.3f\n",
                   name, ((GCControllerAxisInput *)el).value);
        }

        fflush(stdout);
    };
}

int main(int argc, const char **argv)
{
    @autoreleasepool
    {
        const BOOL watching = (argc > 1 && strcmp(argv[1], "watch") == 0);

        /*
         * Without this, GameController delivers input only while the app is
         * frontmost — and a command-line tool never is, so every button press
         * is silently dropped and the probe looks like the controller is dead.
         */
        GCController.shouldMonitorBackgroundEvents = YES;

        [[NSNotificationCenter defaultCenter]
            addObserverForName:GCControllerDidConnectNotification
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification *note) {
            GCController *c = note.object;

            printf("connected:\n");
            describe(c);

            if (watching)
            {
                watch(c);
            }
        }];

        dump();

        for (GCController *c in [GCController controllers])
        {
            if (watching)
            {
                watch(c);
            }
        }

        // Discovery is asynchronous; give late arrivals a moment.
        [[NSRunLoop currentRunLoop]
            runUntilDate:[NSDate dateWithTimeIntervalSinceNow:
                          watching ? 120.0 : 4.0]];

        if (!watching)
        {
            printf("after 4s: ");
            dump();
        }
    }

    return 0;
}
