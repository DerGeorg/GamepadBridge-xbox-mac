/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "status.h"
#include "permissions.h"

#include <atomic>
#include <csignal>
#include <cstring>
#include <iterator>
#include <map>
#include <mutex>
#include <unistd.h>

namespace
{
    std::mutex mutex;
    std::string message = "Waiting for adapter";

    // Number -> battery level ("" until the controller reports one).
    std::map<int, std::string> controllers;

    void copyOut(const std::string &value, char *buffer, long capacity)
    {
        if (!buffer || capacity <= 0)
        {
            return;
        }

        strlcpy(buffer, value.c_str(), static_cast<size_t>(capacity));
    }
}

void Status::setMessage(const std::string &text)
{
    std::lock_guard<std::mutex> lock(mutex);

    message = text;
}

void Status::controllerConnected(int number)
{
    std::lock_guard<std::mutex> lock(mutex);

    controllers[number] = "";
}

void Status::controllerDisconnected(int number)
{
    std::lock_guard<std::mutex> lock(mutex);

    controllers.erase(number);
}

void Status::setBattery(int number, const std::string &level)
{
    std::lock_guard<std::mutex> lock(mutex);

    // A late report from a controller that has just left must not bring
    // its line back.
    auto controller = controllers.find(number);

    if (controller != controllers.end())
    {
        controller->second = level;
    }
}

void gpb_status_connection(char *buffer, long capacity)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (!message.empty())
    {
        copyOut(message, buffer, capacity);
    }

    else
    {
        copyOut(controllers.empty() ? "No controller" : "", buffer, capacity);
    }
}

int gpb_status_controller_count(void)
{
    std::lock_guard<std::mutex> lock(mutex);

    return static_cast<int>(controllers.size());
}

void gpb_status_controller(int index, char *buffer, long capacity)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (index < 0 || index >= static_cast<int>(controllers.size()))
    {
        copyOut("", buffer, capacity);

        return;
    }

    auto controller = std::next(controllers.begin(), index);

    std::string line = "Controller " + std::to_string(controller->first);

    if (!controller->second.empty())
    {
        line += " \u00b7 Battery: " + controller->second;
    }

    copyOut(line, buffer, capacity);
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
