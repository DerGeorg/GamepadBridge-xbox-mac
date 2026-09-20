/*
 * Copyright (C) 2019 Medusalix
 * Copyright (C) 2026 GamepadBridge contributors (macOS port)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*
 * macOS port of xow's entry point (originally xow.cpp).
 *
 * The Linux original used signalfd() to integrate signal delivery with an
 * interruptible reader. macOS has no signalfd, so this version blocks the
 * relevant signals and waits for them with sigwait() on the main thread.
 * The dongle's USB worker threads inherit the blocked mask, so signals are
 * handled in exactly one place.
 *
 * SIGINT / SIGTERM -> shut down cleanly.
 * SIGUSR1          -> toggle the dongle into pairing mode.
 */

#include "utils/log.h"
#include "output.h"
#include "status.h"
#include "firmware.h"
#include "permissions.h"
#include "dongle/usb.h"
#include "dongle/dongle.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <sys/types.h>

#ifndef GAMEPADBRIDGE_VERSION
#define GAMEPADBRIDGE_VERSION "dev"
#endif

#ifdef GAMEPADBRIDGE_MENUBAR
extern "C" void gpb_menubar_run(void);
extern "C" void gpb_menubar_stop(void);
extern "C" int gpb_confirm_firmware(const char *message);

#endif

namespace
{
    /*
     * macOS gates publishing a virtual HID device behind Input Monitoring,
     * and refuses silently: CoreHID just returns nil, with no prompt and
     * nothing in the log. So ask for it explicitly rather than letting the
     * app come up looking fine and doing nothing.
     *
     * Returns false only when the user chooses to quit. Otherwise it carries
     * on without the permission, because macOS offers its own "Quit and
     * Reopen" once the switch is flipped — and only for an app that is
     * actually running.
     */
    bool ensurePermissions()
    {
        if (Permissions::allGranted())
        {
            return true;
        }

        // Also when already denied: asking again is what puts the app into
        // the lists, and one that is not listed cannot be switched on at all.
        Log::info("Asking for the permissions macOS requires...");

        Permissions::request();

        if (Permissions::allGranted())
        {
            return true;
        }

        Log::error("Input Monitoring and Accessibility are not both granted.");

        Status::setConnection("Needs permissions", false);

#ifdef GAMEPADBRIDGE_MENUBAR
        // The menu bar shows a window that stays up and watches for them
        // itself; it cannot appear before the event loop runs.
        gpb_set_needs_permission(1);
#else
        Log::error("%s", gpb_permission_message());
        Log::error("Grant them to whichever app launches this binary, not to "
                   "the binary itself.");
#endif

        return true;
    }

    /*
     * Ask before fetching the firmware: it is Microsoft's, and the user is the
     * one accepting their terms. The menu bar build has a window server and
     * asks in a dialog; otherwise this only works on a terminal, and refusing
     * to guess is better than downloading on someone's behalf.
     */
    bool acquireFirmware(const std::string &path)
    {
        const std::string notice = Firmware::notice();

#ifdef GAMEPADBRIDGE_MENUBAR
        if (!gpb_confirm_firmware(notice.c_str()))
        {
            Log::info("Firmware download declined");

            return false;
        }
#else
        if (!isatty(STDIN_FILENO))
        {
            Log::error("The firmware is missing and there is no terminal to "
                       "ask on.");
            Log::error("Run scripts/get-firmware.sh, or point XOW_FIRMWARE at "
                       "an existing copy.");

            return false;
        }

        printf("\n%s\n\nDownload it now? [y/N]: ", notice.c_str());
        fflush(stdout);

        int answer = getchar();

        if (answer != 'y' && answer != 'Y')
        {
            Log::info("Firmware download declined");

            return false;
        }
#endif

        return Firmware::download(path);
    }

    /*
     * Hardware-free smoke test (GAMEPADBRIDGE_SELFTEST=1).
     *
     * Publishes the virtual gamepad and walks every input in turn, without the
     * dongle, a paired controller or any USB at all. Each step is announced
     * before it is driven, so pairing this log with a watcher on the other end
     * (tools/gc-probe.m) yields the mapping macOS actually applies — with no
     * human in the loop to press things in the wrong order, which is exactly
     * how a button map gets "measured" into the wrong answer.
     */
    struct Step
    {
        const char *name;
        std::function<void(GamepadState &)> apply;
    };

    const Step kSteps[] =
    {
        { "a",                [](GamepadState &s) { s.a = true; } },
        { "b",                [](GamepadState &s) { s.b = true; } },
        { "x",                [](GamepadState &s) { s.x = true; } },
        { "y",                [](GamepadState &s) { s.y = true; } },
        { "bumperLeft",       [](GamepadState &s) { s.bumperLeft = true; } },
        { "bumperRight",      [](GamepadState &s) { s.bumperRight = true; } },
        { "select (view)",    [](GamepadState &s) { s.select = true; } },
        { "start (menu)",     [](GamepadState &s) { s.start = true; } },
        { "thumbLeft (LS)",   [](GamepadState &s) { s.thumbLeft = true; } },
        { "thumbRight (RS)",  [](GamepadState &s) { s.thumbRight = true; } },
        { "guide (xbox)",     [](GamepadState &s) { s.guide = true; } },
        { "dpadUp",           [](GamepadState &s) { s.dpadUp = true; } },
        { "dpadRight",        [](GamepadState &s) { s.dpadRight = true; } },
        { "dpadDown",         [](GamepadState &s) { s.dpadDown = true; } },
        { "dpadLeft",         [](GamepadState &s) { s.dpadLeft = true; } },
        { "leftStick right",  [](GamepadState &s) { s.stickLeftX = 32767; } },
        { "leftStick up",     [](GamepadState &s) { s.stickLeftY = 32767; } },
        { "rightStick right", [](GamepadState &s) { s.stickRightX = 32767; } },
        { "rightStick up",    [](GamepadState &s) { s.stickRightY = 32767; } },
        { "triggerLeft",      [](GamepadState &s) { s.triggerLeft = 1023; } },
        { "triggerRight",     [](GamepadState &s) { s.triggerRight = 1023; } },
    };

    int runSelfTest(const sigset_t &mask)
    {
        std::unique_ptr<OutputDevice> output = makeOutputDevice();

        DeviceInfo info;
        info.version = 1;
        info.name = "GamepadBridge Self-Test";

        output->create(info);

        std::atomic<bool> running(true);

        std::thread pump([&output, &running]() {
            const size_t total = sizeof(kSteps) / sizeof(kSteps[0]);

            // Hold each input for a beat and release it again, so a watcher
            // sees exactly one press and one release per announced step.
            auto hold = [&output, &running](const GamepadState &state, int ms) {
                for (int elapsed = 0; elapsed < ms && running; elapsed += 8)
                {
                    output->update(state);

                    std::this_thread::sleep_for(std::chrono::milliseconds(8));
                }
            };

            while (running)
            {
                for (size_t i = 0; i < total && running; i++)
                {
                    GamepadState state;

                    kSteps[i].apply(state);

                    Log::info("self-test %2zu/%zu: %s",
                              i + 1, total, kSteps[i].name);

                    hold(state, 700);
                    hold(GamepadState(), 400);
                }

                if (running)
                {
                    Log::info("self-test: sweep complete, starting over");
                }
            }
        });

        Log::info("Self-test: walking every input in turn. Ctrl-C to stop.");

        int received = 0;

        while (sigwait(&mask, &received) == 0 && received == SIGUSR1)
        {
            // Pairing has no meaning here; keep waiting for INT/TERM.
        }

        running = false;
        pump.join();

        return EXIT_SUCCESS;
    }
}

namespace
{
    // The dongle half of the program: everything from finding the adapter to
    // the signal loop that shuts it down again. Factored out of main() so the
    // menu bar build can run it on a worker thread and leave the main thread
    // to AppKit, which insists on having it.
    int runDriver(const sigset_t &mask)
    {
        UsbDeviceManager manager;

        // A failing USB transfer asks us to terminate: deliver a process-wide
        // SIGTERM that the sigwait() loop below will pick up.
        UsbDevice::Terminate terminate = []() {
            kill(getpid(), SIGTERM);
        };

        // While waiting for the dongle to be plugged in, allow Ctrl-C to quit
        // (default disposition terminates the process).
        if (pthread_sigmask(SIG_UNBLOCK, &mask, nullptr) != 0)
        {
            Log::error("Error unblocking signals: %s", strerror(errno));

            return EXIT_FAILURE;
        }

        /*
         * Opening the dongle can legitimately fail on a first try: a reset makes
         * macOS re-enumerate it, so the device we just found is briefly gone.
         * Retry instead of letting the exception escape main() — an uncaught
         * throw aborts the process and buries a transient hiccup under a crash
         * report.
         */
        std::unique_ptr<UsbDevice> device;

        for (int attempt = 1; !device; attempt++)
        {
            try
            {
                device = manager.getDevice({
                    { DONGLE_VID, DONGLE_PID_OLD },
                    { DONGLE_VID, DONGLE_PID_NEW },
                    { DONGLE_VID, DONGLE_PID_SURFACE }
                }, terminate);
            }

            catch (const UsbException &error)
            {
                if (attempt >= 5)
                {
                    Log::error("Could not open the dongle: %s", error.what());
                    Log::error("Unplug the adapter, plug it back in, and make "
                               "sure no second instance is running.");

                    return EXIT_FAILURE;
                }

                Log::info("%s - retrying (%d/5)", error.what(), attempt);

                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }

        // Re-block before starting the dongle's worker threads.
        if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0)
        {
            Log::error("Error blocking signals: %s", strerror(errno));

            return EXIT_FAILURE;
        }

        Dongle dongle(std::move(device));

        Status::setConnection("No controller", false);

        Log::info("Ready. Press the dongle button (or send SIGUSR1) to pair.");

        while (true)
        {
            int signal = 0;

            if (sigwait(&mask, &signal) != 0)
            {
                Log::error("Error waiting for signal: %s", strerror(errno));

                break;
            }

            if (signal == SIGUSR1)
            {
                Log::debug("User signal received, enabling pairing");

                // Hand the work to the USB thread; libusb must stay single-threaded.
                dongle.enablePairing();

                continue;
            }

            // SIGINT or SIGTERM
            break;
        }

        Log::info("Shutting down...");

        return EXIT_SUCCESS;
    }
}

int main()
{
    Log::init();
    Log::info("GamepadBridge %s (based on xow by Medusalix)", GAMEPADBRIDGE_VERSION);

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGUSR1);

    // Ensure none of these signals is inherited as ignored (SIG_IGN). A
    // job-control shell sets SIGINT/SIGQUIT to SIG_IGN for background jobs,
    // and on BSD/macOS an ignored signal is discarded even while blocked, so
    // sigwait() below would never see it. Resetting to the default keeps
    // Ctrl-C working whether we run in the foreground or background.
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGUSR1, SIG_DFL);

    // Block signals so every thread we spawn later inherits the block.
    if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0)
    {
        Log::error("Error blocking signals: %s", strerror(errno));

        return EXIT_FAILURE;
    }

    if (std::getenv("GAMEPADBRIDGE_SELFTEST"))
    {
        Log::info("Self-test mode: publishing the virtual gamepad, no dongle needed.");

        return runSelfTest(mask);
    }

    // Permissions first: granting them means restarting, and finding that
    // out after a firmware download and a pairing dance is worse.
    if (!ensurePermissions())
    {
        Log::info("Shutting down...");

        return EXIT_SUCCESS;
    }

    /*
     * Fetch the firmware before anything else starts: this runs on the main
     * thread, which is where a dialog is allowed to appear.
     */
    const std::string firmware = Firmware::resolvePath();

    if (!Firmware::isPresent(firmware) && !acquireFirmware(firmware))
    {
        return EXIT_FAILURE;
    }

#ifdef GAMEPADBRIDGE_MENUBAR
    /*
     * AppKit owns the main thread, so the driver moves to a worker. Quitting
     * from the menu raises SIGTERM, the driver's own signal loop unwinds and
     * destroys the dongle, and only then is the event loop stopped — so the
     * controller is still powered down properly on the way out.
     */
    int result = EXIT_SUCCESS;

    std::thread driver([&result, &mask]() {
        result = runDriver(mask);

        gpb_menubar_stop();
    });

    gpb_menubar_run();

    driver.join();

    return result;
#else
    return runDriver(mask);
#endif
}
