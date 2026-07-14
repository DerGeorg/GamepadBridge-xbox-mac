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

#include "usb.h"
#include "../utils/log.h"

#include <sys/time.h>

// Timeouts in milliseconds
#define USB_TIMEOUT_WRITE 1000

// Per-reader state for the asynchronous bulk IN transfers.
struct UsbDevice::Reader
{
    UsbDevice *owner;
    uint8_t endpoint;
    ReadCallback callback;
    std::vector<uint8_t> buffer;
    libusb_transfer *transfer;
};

UsbDevice::UsbDevice(
    libusb_device *device,
    Terminate terminate
) : terminate(terminate)
{
    Log::debug("Opening device...");

    int error = libusb_open(device, &handle);

    if (error)
    {
        throw UsbException("Error opening device", error);
    }

    error = libusb_reset_device(handle);

    if (error)
    {
        throw UsbException("Error resetting device", error);
    }

    error = libusb_set_configuration(handle, 1);

    if (error)
    {
        throw UsbException("Error setting configuration", error);
    }

    error = libusb_claim_interface(handle, 0);

    if (error)
    {
        throw UsbException("Error claiming interface", error);
    }
}

UsbDevice::~UsbDevice()
{
    Log::debug("Closing device...");

    // Readers are normally torn down via stopReaders() before any shutdown
    // writes happen. Anything still allocated here had no active event pump
    // left to service a cancellation, so just free the transfer structs.
    for (std::unique_ptr<Reader> &reader : readers)
    {
        libusb_free_transfer(reader->transfer);
    }

    int error = libusb_release_interface(handle, 0);

    if (error)
    {
        Log::error(
            "Error releasing interface: %s",
            libusb_error_name(error)
        );
    }

    libusb_close(handle);
}

void UsbDevice::controlTransfer(ControlPacket packet, bool write)
{
    uint8_t direction = write ? LIBUSB_ENDPOINT_OUT : LIBUSB_ENDPOINT_IN;

    // Number of bytes or error code
    int transferred = libusb_control_transfer(
        handle,
        LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | direction,
        packet.request,
        packet.value,
        packet.index,
        packet.data,
        packet.length,
        USB_TIMEOUT_WRITE
    );

    if (transferred != packet.length)
    {
        Log::error(
            "Error in control transfer: %s",
            libusb_error_name(transferred)
        );

        terminate();
    }
}

void UsbDevice::addReader(uint8_t endpoint, ReadCallback callback)
{
    std::unique_ptr<Reader> reader(new Reader());

    reader->owner = this;
    reader->endpoint = endpoint;
    reader->callback = std::move(callback);
    reader->buffer.resize(USB_MAX_BULK_TRANSFER_SIZE);
    reader->transfer = libusb_alloc_transfer(0);

    if (!reader->transfer)
    {
        throw UsbException("Error allocating transfer", LIBUSB_ERROR_NO_MEM);
    }

    // Timeout 0 = wait indefinitely; the dongle streams whenever it has data.
    libusb_fill_bulk_transfer(
        reader->transfer,
        handle,
        endpoint | LIBUSB_ENDPOINT_IN,
        reader->buffer.data(),
        static_cast<int>(reader->buffer.size()),
        &UsbDevice::onReadComplete,
        reader.get(),
        0
    );

    int error = libusb_submit_transfer(reader->transfer);

    if (error)
    {
        libusb_free_transfer(reader->transfer);

        throw UsbException("Error submitting read transfer", error);
    }

    activeReaders++;
    readers.push_back(std::move(reader));
}

void LIBUSB_CALL UsbDevice::onReadComplete(libusb_transfer *transfer)
{
    Reader *reader = static_cast<Reader*>(transfer->user_data);
    UsbDevice *self = reader->owner;

    // Shutting down or cancelled: stop resubmitting and let stopReaders()
    // know this reader is finished.
    if (self->stopping.load() ||
        transfer->status == LIBUSB_TRANSFER_CANCELLED)
    {
        self->activeReaders--;

        return;
    }

    if (transfer->status == LIBUSB_TRANSFER_COMPLETED)
    {
        if (transfer->actual_length > 0)
        {
            Bytes data(
                transfer->buffer,
                transfer->buffer + transfer->actual_length
            );

            reader->callback(data);
        }
    }

    else if (transfer->status != LIBUSB_TRANSFER_TIMED_OUT)
    {
        Log::error(
            "Read transfer failed on endpoint %u (status %d)",
            reader->endpoint,
            transfer->status
        );

        self->activeReaders--;
        self->terminate();

        return;
    }

    // Keep the endpoint continuously readable.
    int error = libusb_submit_transfer(transfer);

    if (error)
    {
        Log::error(
            "Failed to resubmit read transfer: %s",
            libusb_error_name(error)
        );

        self->activeReaders--;
        self->terminate();
    }
}

void UsbDevice::pumpEvents(int timeoutMs)
{
    timeval tv;

    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int error = libusb_handle_events_timeout_completed(nullptr, &tv, nullptr);

    if (error && error != LIBUSB_ERROR_TIMEOUT)
    {
        Log::error("Error handling events: %s", libusb_error_name(error));

        terminate();
    }
}

void UsbDevice::stopReaders()
{
    stopping = true;

    for (std::unique_ptr<Reader> &reader : readers)
    {
        libusb_cancel_transfer(reader->transfer);
    }

    // Drive the event loop until every cancellation has been delivered.
    while (activeReaders.load() > 0)
    {
        pumpEvents(10);
    }

    for (std::unique_ptr<Reader> &reader : readers)
    {
        libusb_free_transfer(reader->transfer);
    }

    readers.clear();
}

bool UsbDevice::bulkWrite(uint8_t endpoint, Bytes &data)
{
    int transferred = 0;
    int error = libusb_bulk_transfer(
        handle,
        endpoint | LIBUSB_ENDPOINT_OUT,
        data.raw(),
        data.size(),
        &transferred,
        USB_TIMEOUT_WRITE
    );

    if (error)
    {
        Log::error("Error in bulk write: %s", libusb_error_name(error));

        terminate();

        return false;
    }

    return true;
}

UsbDeviceManager::UsbDeviceManager()
{
    int error = libusb_init(nullptr);

    if (error)
    {
        throw UsbException("Error initializing libusb", error);
    }
}

UsbDeviceManager::~UsbDeviceManager()
{
    libusb_exit(nullptr);
}

std::unique_ptr<UsbDevice> UsbDeviceManager::getDevice(
    std::initializer_list<HardwareId> ids,
    UsbDevice::Terminate terminate
) {
    std::vector<libusb_hotplug_callback_handle> handles(ids.size());
    size_t counter = 0;
    libusb_device *device = nullptr;

    for (HardwareId id : ids)
    {
        int error = libusb_hotplug_register_callback(
            nullptr,
            static_cast<libusb_hotplug_event>(
                LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED
            ),
            LIBUSB_HOTPLUG_ENUMERATE,
            id.vendorId,
            id.productId,
            LIBUSB_HOTPLUG_MATCH_ANY,
            hotplugCallback,
            &device,
            &handles[counter]
        );

        if (error)
        {
            throw UsbException("Error registering hotplug", error);
        }

        counter++;
    }

    Log::info("Waiting for device...");

    // Handle events until device is plugged in
    while (!device)
    {
        int error = libusb_handle_events_completed(nullptr, nullptr);

        if (error)
        {
            throw UsbException("Error handling events", error);
        }
    }

    // Remove all hotplug callbacks
    for (libusb_hotplug_callback_handle handle : handles)
    {
        libusb_hotplug_deregister_callback(nullptr, handle);
    }

    // Pass ownership of device to caller
    return std::unique_ptr<UsbDevice>(new UsbDevice(
        device,
        terminate
    ));
}

int UsbDeviceManager::hotplugCallback(
    libusb_context * /*context*/,
    libusb_device *device,
    libusb_hotplug_event /*event*/,
    void *userData
) {
    libusb_device **newDevice = static_cast<libusb_device**>(userData);

    *newDevice = device;

    // Deregister hotplug callback
    return 1;
}

UsbException::UsbException(
    std::string message,
    int error
) : std::runtime_error(message + ": " + libusb_error_name(error)) {}
