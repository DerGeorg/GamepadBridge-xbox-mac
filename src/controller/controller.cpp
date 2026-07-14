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

#include "controller.h"
#include "../utils/log.h"

#include <cstdlib>
#include <string>

// Compatibility mode: present as an Xbox 360 pad. Some games match the
// controller name/PID against hardcoded values. Used by Stage 2 backends.
#define COMPATIBILITY_ENV "XOW_COMPATIBILITY"
#define COMPATIBILITY_NAME "Microsoft X-Box 360 pad"
#define COMPATIBILITY_PID 0x028e
#define COMPATIBILITY_VERSION 0x0104

// The controller itself uses device id 0; accessories use ids > 0.
#define DEVICE_ID_CONTROLLER 0
#define DEVICE_NAME "Xbox One Wireless Controller"

Controller::Controller(
    SendPacket sendPacket
) : GipDevice(sendPacket),
    output(makeOutputDevice()) {}

Controller::~Controller()
{
    if (!setPowerMode(DEVICE_ID_CONTROLLER, POWER_OFF))
    {
        Log::error("Failed to turn off controller");
    }
}

void Controller::deviceAnnounced(uint8_t /*id*/, const AnnounceData *announce)
{
    Log::info("Device announced, product id: %04x", announce->productId);
    Log::debug(
        "Firmware version: %d.%d.%d.%d",
        announce->firmwareVersion.major,
        announce->firmwareVersion.minor,
        announce->firmwareVersion.build,
        announce->firmwareVersion.revision
    );
    Log::debug(
        "Hardware version: %d.%d.%d.%d",
        announce->hardwareVersion.major,
        announce->hardwareVersion.minor,
        announce->hardwareVersion.build,
        announce->hardwareVersion.revision
    );

    initInput(announce);
}

void Controller::statusReceived(uint8_t /*id*/, const StatusData *status)
{
    const std::string levels[] = { "empty", "low", "medium", "full" };

    uint8_t type = status->batteryType;
    uint8_t level = status->batteryLevel;

    // Controller is charging or level hasn't changed
    if (type == BATT_TYPE_CHARGING || level == batteryLevel)
    {
        return;
    }

    Log::info("Battery level: %s", levels[level].c_str());

    batteryLevel = level;
}

void Controller::guideButtonPressed(const GuideButtonData *button)
{
    state.guide = button->pressed;
    output->update(state);
}

void Controller::serialNumberReceived(const SerialData *serial)
{
    const std::string number(
        serial->serialNumber,
        sizeof(serial->serialNumber)
    );

    Log::info("Serial number: %s", number.c_str());
}

void Controller::inputReceived(const InputData *input)
{
    // Buttons (guide arrives via a separate report, so it is preserved).
    state.a = input->buttons.a;
    state.b = input->buttons.b;
    state.x = input->buttons.x;
    state.y = input->buttons.y;
    state.start = input->buttons.start;
    state.select = input->buttons.select;
    state.bumperLeft = input->buttons.bumperLeft;
    state.bumperRight = input->buttons.bumperRight;
    state.thumbLeft = input->buttons.stickLeft;
    state.thumbRight = input->buttons.stickRight;
    state.dpadUp = input->buttons.dpadUp;
    state.dpadDown = input->buttons.dpadDown;
    state.dpadLeft = input->buttons.dpadLeft;
    state.dpadRight = input->buttons.dpadRight;

    // Triggers (0..1023) and sticks (-32768..32767, +Y = up).
    state.triggerLeft = input->triggerLeft;
    state.triggerRight = input->triggerRight;
    state.stickLeftX = input->stickLeftX;
    state.stickLeftY = input->stickLeftY;
    state.stickRightX = input->stickRightX;
    state.stickRightY = input->stickRightY;

    output->update(state);
}

void Controller::initInput(const AnnounceData *announce)
{
    LedModeData ledMode = {};

    // Dim the LED a little bit, like the original driver.
    // Brightness ranges from 0x00 to 0x20.
    ledMode.mode = LED_ON;
    ledMode.brightness = 0x14;

    if (!setPowerMode(DEVICE_ID_CONTROLLER, POWER_ON))
    {
        Log::error("Failed to set initial power mode");

        return;
    }

    if (!setLedMode(ledMode))
    {
        Log::error("Failed to set initial LED mode");

        return;
    }

    if (!requestSerialNumber())
    {
        Log::error("Failed to request serial number");

        return;
    }

    DeviceInfo info = {};
    info.vendorId = announce->vendorId;

    if (std::getenv(COMPATIBILITY_ENV))
    {
        info.productId = COMPATIBILITY_PID;
        info.version = COMPATIBILITY_VERSION;
        info.name = COMPATIBILITY_NAME;
    }

    else
    {
        info.productId = announce->productId;
        info.version = (announce->firmwareVersion.major << 8) |
            announce->firmwareVersion.minor;
        info.name = DEVICE_NAME;
    }

    output->create(info);
}
