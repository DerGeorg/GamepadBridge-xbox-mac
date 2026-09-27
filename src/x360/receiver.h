/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The Xbox 360 Wireless Receiver: up to four controllers, each published as a
 * virtual pad of its own, next to whatever the Xbox Wireless Adapter carries.
 *
 * It lives on a thread and a libusb context of its own. The adapter's driver
 * owns libusb's default context from its USB thread, and on macOS two threads
 * transferring on one context deadlock (see dongle/usb.h) - so the two never
 * share one.
 *
 * The receiver may come and go at any time: it is looked for every two
 * seconds, taken over when it appears, and let go when it disappears. When
 * another app holds it - Steam does, with a driver of its own - the menu says
 * so, and it is tried again until it is free.
 */

#pragma once

#include "protocol.h"
#include "../output.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct libusb_context;
struct libusb_device_handle;
struct libusb_transfer;

class X360Receiver
{
public:
    X360Receiver();

    // Powers off the controllers it connected, like the adapter does on the
    // way out, and removes their pads.
    ~X360Receiver();

    X360Receiver(const X360Receiver &) = delete;
    X360Receiver &operator=(const X360Receiver &) = delete;

private:
    struct Slot;

    void run();

    bool open();
    void close(bool powerOff);
    void reportBusy(const std::string &owner);

    // Runs `task` on the receiver's thread, soon. From any thread.
    void post(std::function<void()> task);
    void runTasks();

    void process(Slot &slot, const uint8_t *data, int length);
    void connect(Slot &slot);
    void disconnect(Slot &slot);
    void publish(Slot &slot);

    void send(Slot &slot, const X360::Command &command);
    void rumble(Slot &slot, const RumbleEffect &effect);
    void stopExpiredRumble();

    static void onRead(libusb_transfer *transfer);
    static void onWrite(libusb_transfer *transfer);

    libusb_context *context = nullptr;
    libusb_device_handle *handle = nullptr;
    std::vector<std::unique_ptr<Slot>> slots;

    // Set from the read callbacks when the receiver is gone.
    bool lost = false;

    // Writes still in flight; close() waits for them, since their callbacks
    // point into a slot.
    int pendingWrites = 0;

    // Last owner reported, so a receiver held for hours logs it once.
    std::string busyOwner = "-";

    std::mutex taskMutex;
    std::queue<std::function<void()>> tasks;

    std::atomic<bool> stopping{false};
    std::thread thread;
};
