/*
 * Copyright (C) 2019 Medusalix
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

#include "../utils/bytes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <stdexcept>
#include <vector>
#include <atomic>

// The include path to libusb's directory is supplied by pkg-config
// (LIBUSB_INCLUDE_DIRS in CMake), so the header is found directly as
// <libusb.h>. This works identically on Linux and macOS/Homebrew, where
// pkg-config's -I already points into the libusb-1.0 directory.
#include <libusb.h>

#define USB_MAX_BULK_TRANSFER_SIZE 512

/*
 * Base class for interfacing with USB devices
 * Provides control/bulk transfer capabilities
 *
 * macOS port note
 * ---------------
 * The Linux original (xow) read the two bulk IN endpoints from two separate
 * threads using *synchronous* libusb transfers. On macOS this deadlocks:
 * libusb's darwin backend lets only one thread service events at a time, so
 * two threads each blocked in a synchronous transfer wedge each other (one
 * polls forever holding the event lock, the other waits forever for it).
 *
 * The fix is asynchronous reads driven by a single event-pumping thread.
 * Reading is therefore split into:
 *   - addReader()   submit a self-resubmitting async IN transfer.
 *   - pumpEvents()  service libusb events; runs the reader callbacks.
 *   - stopReaders() cancel them and wait until no callback can fire again.
 * The owner (Dongle) drives all three from a single thread, which also
 * issues every write/control transfer, so libusb is only ever touched by
 * one thread at a time.
 */
class UsbDevice
{
public:
    using Terminate = std::function<void()>;
    using ReadCallback = std::function<void(const Bytes &)>;

    struct ControlPacket
    {
        uint8_t request;
        uint16_t value;
        uint16_t index;
        uint8_t *data;
        uint16_t length;
    };

    UsbDevice(libusb_device *device, Terminate terminate);
    virtual ~UsbDevice();

    void controlTransfer(ControlPacket packet, bool write);
    bool bulkWrite(uint8_t endpoint, Bytes &data);

    // Submit a continuous asynchronous reader on a bulk IN endpoint.
    // `callback` is invoked from pumpEvents() (on the caller's thread) for
    // each received transfer; keep it non-blocking and do not call back into
    // a blocking transfer from it.
    void addReader(uint8_t endpoint, ReadCallback callback);

    // Service libusb events for up to `timeoutMs`, running reader callbacks.
    void pumpEvents(int timeoutMs);

    // Cancel all readers and block until their callbacks can no longer fire.
    // Must be called on the same thread that calls pumpEvents().
    void stopReaders();

private:
    struct Reader;
    static void LIBUSB_CALL onReadComplete(libusb_transfer *transfer);

    libusb_device_handle *handle;
    Terminate terminate;

    std::vector<std::unique_ptr<Reader>> readers;
    std::atomic<int> activeReaders{0};
    std::atomic<bool> stopping{false};
};

/*
 * Provides access to USB devices
 * Handles device enumeration and hot plugging
 */
class UsbDeviceManager
{
public:
    struct HardwareId
    {
        uint16_t vendorId, productId;
    };

    UsbDeviceManager();
    ~UsbDeviceManager();

    std::unique_ptr<UsbDevice> getDevice(
        std::initializer_list<HardwareId> ids,
        UsbDevice::Terminate terminate
    );

private:
    static int hotplugCallback(
        libusb_context *context,
        libusb_device *device,
        libusb_hotplug_event event,
        void *userData
    );
};

class UsbException : public std::runtime_error
{
public:
    UsbException(std::string message, int error);
};
