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

#pragma once

#include "gip.h"
#include "../output.h"

#include <memory>

/*
 * Translates GIP gamepad events into a normalized GamepadState and forwards
 * them to an OutputDevice backend.
 *
 * macOS port: the original Linux version drove a uinput device and handled
 * force-feedback uploads from the kernel. That OS-specific code lived in
 * controller/input.{h,cpp} and is replaced here by the OutputDevice
 * abstraction (see ../output.h). The class interface used by Dongle
 * (constructor + inherited handlePacket) is unchanged.
 */
class Controller : public GipDevice
{
public:
    Controller(SendPacket sendPacket);
    ~Controller();

private:
    /* GIP events */
    void deviceAnnounced(uint8_t id, const AnnounceData *announce) override;
    void statusReceived(uint8_t id, const StatusData *status) override;
    void guideButtonPressed(const GuideButtonData *button) override;
    void serialNumberReceived(const SerialData *serial) override;
    void inputReceived(const InputData *input) override;

    /* Device initialization */
    void initInput(const AnnounceData *announce);

    std::unique_ptr<OutputDevice> output;
    GamepadState state;

    uint8_t batteryLevel = 0xff;
};
