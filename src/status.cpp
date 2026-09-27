/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "status.h"
#include "permissions.h"

#include <atomic>
#include <csignal>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>
#include <unistd.h>

namespace
{
    std::mutex mutex;
    std::string message;

    struct Controller
    {
        std::string label;
        std::string battery;   // "" until the controller reports one
    };

    std::map<int, Controller> controllers;

    bool adapterPresent = false;
    bool receiverPresent = false;
    std::string receiverOwner;

    void copyOut(const std::string &value, char *buffer, long capacity)
    {
        if (!buffer || capacity <= 0)
        {
            return;
        }

        strlcpy(buffer, value.c_str(), static_cast<size_t>(capacity));
    }

    // Built on demand under the lock; a handful of lines, once a second.
    std::vector<std::string> lines()
    {
        std::vector<std::string> out;

        if (!message.empty())
        {
            out.push_back(message);
        }

        if (controllers.empty())
        {
            if (message.empty())
            {
                out.push_back("No controller");
            }

            return out;
        }

        for (const auto &entry : controllers)
        {
            std::string line = entry.second.label;

            if (!entry.second.battery.empty())
            {
                line += " \u00b7 Battery: " + entry.second.battery;
            }

            out.push_back(line);
        }

        return out;
    }
}

void Status::setMessage(const std::string &text)
{
    std::lock_guard<std::mutex> lock(mutex);

    message = text;
}

void Status::setReceiverOwner(const std::string &name)
{
    std::lock_guard<std::mutex> lock(mutex);

    receiverOwner = name;
}

void gpb_status_receiver_owner(char *buffer, long capacity)
{
    std::lock_guard<std::mutex> lock(mutex);

    copyOut(receiverOwner, buffer, capacity);
}

void Status::controllerConnected(int key, const std::string &label)
{
    std::lock_guard<std::mutex> lock(mutex);

    controllers[key] = Controller{ label, "" };
}

void Status::controllerDisconnected(int key)
{
    std::lock_guard<std::mutex> lock(mutex);

    controllers.erase(key);
}

void Status::setBattery(int key, const std::string &level)
{
    std::lock_guard<std::mutex> lock(mutex);

    // A late report from a controller that has just left must not bring
    // its line back.
    auto controller = controllers.find(key);

    if (controller != controllers.end())
    {
        controller->second.battery = level;
    }
}

void Status::setAdapterPresent(bool present)
{
    std::lock_guard<std::mutex> lock(mutex);

    adapterPresent = present;
}

void Status::setReceiverPresent(bool present)
{
    std::lock_guard<std::mutex> lock(mutex);

    receiverPresent = present;
}

int gpb_status_adapter_present(void)
{
    std::lock_guard<std::mutex> lock(mutex);

    return adapterPresent ? 1 : 0;
}

int gpb_status_receiver_present(void)
{
    std::lock_guard<std::mutex> lock(mutex);

    return receiverPresent ? 1 : 0;
}

int gpb_status_line_count(void)
{
    std::lock_guard<std::mutex> lock(mutex);

    return static_cast<int>(lines().size());
}

void gpb_status_line(int index, char *buffer, long capacity)
{
    std::lock_guard<std::mutex> lock(mutex);

    const std::vector<std::string> all = lines();

    copyOut(index >= 0 && index < static_cast<int>(all.size()) ? all[index] : "",
            buffer, capacity);
}

int gpb_status_controller_present(void)
{
    std::lock_guard<std::mutex> lock(mutex);

    return controllers.empty() ? 0 : 1;
}

void gpb_request_pairing(void)
{
    kill(getpid(), SIGUSR1);
}

void gpb_request_quit(void)
{
    kill(getpid(), SIGTERM);
}

namespace
{
    std::atomic<bool> needsPermission{false};
}

void gpb_set_needs_permission(int needed)
{
    needsPermission = needed != 0;
}

int gpb_needs_permission(void)
{
    return needsPermission ? 1 : 0;
}
