/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "status.h"

#include <csignal>
#include <cstring>
#include <mutex>
#include <unistd.h>

namespace
{
    std::mutex mutex;
    std::string connection = "Waiting for adapter";
    std::string battery;
    bool present = false;

    void copyOut(const std::string &value, char *buffer, long capacity)
    {
        if (!buffer || capacity <= 0)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex);

        strlcpy(buffer, value.c_str(), static_cast<size_t>(capacity));
    }
}

void Status::setConnection(const std::string &text, bool controllerPresent)
{
    std::lock_guard<std::mutex> lock(mutex);

    connection = text;
    present = controllerPresent;

    if (!controllerPresent)
    {
        battery.clear();
    }
}

void Status::setBattery(const std::string &text)
{
    std::lock_guard<std::mutex> lock(mutex);

    battery = text;
}

// copyOut takes the lock itself, so these must not hold it here.
void gpb_status_connection(char *buffer, long capacity)
{
    copyOut(connection, buffer, capacity);
}

void gpb_status_battery(char *buffer, long capacity)
{
    copyOut(battery, buffer, capacity);
}

int gpb_status_controller_present(void)
{
    std::lock_guard<std::mutex> lock(mutex);

    return present ? 1 : 0;
}

void gpb_request_pairing(void)
{
    kill(getpid(), SIGUSR1);
}

void gpb_request_quit(void)
{
    kill(getpid(), SIGTERM);
}
