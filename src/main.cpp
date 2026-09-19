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
#include "dongle/usb.h"
#include "dongle/dongle.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>

#ifndef GAMEPADBRIDGE_VERSION
#define GAMEPADBRIDGE_VERSION "dev"
#endif

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

    std::unique_ptr<UsbDevice> device = manager.getDevice({
        { DONGLE_VID, DONGLE_PID_OLD },
        { DONGLE_VID, DONGLE_PID_NEW },
        { DONGLE_VID, DONGLE_PID_SURFACE }
    }, terminate);

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
