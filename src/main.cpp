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
#include "dongle/usb.h"
#include "dongle/dongle.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <sys/types.h>

#ifndef GAMEPADBRIDGE_VERSION
#define GAMEPADBRIDGE_VERSION "dev"
#endif

namespace
{
    /*
     * Hardware-free smoke test (GAMEPADBRIDGE_SELFTEST=1).
     *
     * Publishes the virtual gamepad and wiggles it, without the dongle, a
     * paired controller or any USB at all. That makes the expensive question
     * — does macOS actually surface this device to games? — answerable in a
     * few seconds, which matters because the answer depends on identity and
     * transport settings that have to be tried in combination.
     */
    int runSelfTest(const sigset_t &mask)
    {
        std::unique_ptr<OutputDevice> output = makeOutputDevice();

        DeviceInfo info;
        info.version = 1;
        info.name = "GamepadBridge Self-Test";

        output->create(info);

        std::atomic<bool> running(true);

        std::thread pump([&output, &running]() {
            GamepadState state;

            for (int tick = 0; running; tick++)
            {
                const double phase = tick * 0.05;

                state.stickLeftX = static_cast<int16_t>(30000 * std::sin(phase));
                state.stickLeftY = static_cast<int16_t>(30000 * std::cos(phase));

                // Cycle A/B/X/Y roughly once per second so the buttons move too.
                state.a = (tick / 30) % 4 == 0;
                state.b = (tick / 30) % 4 == 1;
                state.x = (tick / 30) % 4 == 2;
                state.y = (tick / 30) % 4 == 3;

                output->update(state);

                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
        });

        Log::info("Self-test: left stick is circling and A/B/X/Y cycle. Ctrl-C to stop.");

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
