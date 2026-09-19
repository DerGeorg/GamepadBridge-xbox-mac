/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Locating and, if need be, fetching the adapter's firmware.
 *
 * The blob is Microsoft's and is not redistributable, so it cannot ship
 * inside the app. It is downloaded from Microsoft's own servers on first run,
 * exactly as xow does on Linux, and verified against a known SHA-256 — which
 * is what makes the plain-HTTP URL Microsoft publishes acceptable.
 */

#pragma once

#include <string>

namespace Firmware
{
    // XOW_FIRMWARE if set, else an already-installed system-wide copy, else
    // the per-user location we would download to.
    std::string resolvePath();

    bool isPresent(const std::string &path);

    // What the user is agreeing to before anything is fetched.
    std::string notice();

    bool download(const std::string &destination);
}
