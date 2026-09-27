/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "receiver.h"
#include "../status.h"
#include "../utils/log.h"

#include <libusb.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>

namespace
{
    using Clock = std::chrono::steady_clock;

    // How often a receiver that is missing or held elsewhere is looked for.
    const auto RETRY_INTERVAL = std::chrono::seconds(2);

    // The Status key of a slot: past the adapter's 1-16, in slot order.
    int statusKey(int slot)
    {
        return 100 + slot;
    }

    /*
     * Which process holds the receiver, from the IO registry: macOS records
     * it on each interface as "pid 59785, steam_osx". libusb only says
     * LIBUSB_ERROR_ACCESS, which is not something to put in a menu.
     */
    std::string exclusiveOwner(uint16_t productId)
    {
        CFMutableDictionaryRef match = IOServiceMatching("IOUSBHostInterface");

        if (!match)
        {
            return "";
        }

        int vendor = X360::VENDOR_ID, product = productId;
        CFNumberRef vendorNumber = CFNumberCreate(nullptr, kCFNumberIntType, &vendor);
        CFNumberRef productNumber = CFNumberCreate(nullptr, kCFNumberIntType, &product);

        CFDictionarySetValue(match, CFSTR("idVendor"), vendorNumber);
        CFDictionarySetValue(match, CFSTR("idProduct"), productNumber);
        CFRelease(vendorNumber);
        CFRelease(productNumber);

        io_iterator_t iterator = 0;

        // Consumes `match`.
        if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &iterator) != KERN_SUCCESS)
        {
            return "";
        }

        std::string owner;
        io_service_t service;

        while (owner.empty() && (service = IOIteratorNext(iterator)))
        {
            CFTypeRef value = IORegistryEntryCreateCFProperty(
                service, CFSTR("UsbExclusiveOwner"), kCFAllocatorDefault, 0);

            if (value && CFGetTypeID(value) == CFStringGetTypeID())
            {
                char text[256];

                if (CFStringGetCString(static_cast<CFStringRef>(value), text,
                                       sizeof(text), kCFStringEncodingUTF8))
                {
                    owner = text;
                }
            }

            if (value)
            {
                CFRelease(value);
            }

            IOObjectRelease(service);
        }

        IOObjectRelease(iterator);

        // "pid 59785, steam_osx" -> "steam_osx"
        const size_t comma = owner.find(", ");

        return comma == std::string::npos ? owner : owner.substr(comma + 2);
    }

    // "steam_osx" means nothing to most people; "Steam" does.
    std::string friendlyName(std::string process)
    {
        std::string lower = process;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (lower.find("steam") != std::string::npos)
        {
            return "Steam";
        }

        return process.empty() ? "another app" : process;
    }
}

struct X360Receiver::Slot
{
    X360Receiver *receiver = nullptr;

    int number = 0;              // 1-4, as the ring shows it
    int interface = 0;
    uint8_t in = 0, out = 0;

    libusb_transfer *read = nullptr;
    uint8_t buffer[X360::PACKET_SIZE] = {};
    bool reading = false;

    bool present = false;
    std::string identity;

    // Bumped on every disconnect, so rumble queued for the controller that
    // left does not reach the next one in the same slot.
    unsigned generation = 0;

    std::unique_ptr<OutputDevice> output;
    GamepadState state;

    // What the motors are doing, since a request may set only one of them.
    uint8_t strong = 0, weak = 0;
    bool rumbleTimed = false;
    Clock::time_point rumbleUntil;

    // First failed command is logged, the rest are not.
    bool writeFailed = false;
};

X360Receiver::X360Receiver()
{
    if (libusb_init(&context) != LIBUSB_SUCCESS)
    {
        Log::error("[x360] Could not start libusb for the receiver");

        context = nullptr;

        return;
    }

    thread = std::thread(&X360Receiver::run, this);
}

X360Receiver::~X360Receiver()
{
    if (!context)
    {
        return;
    }

    stopping = true;
    libusb_interrupt_event_handler(context);

    if (thread.joinable())
    {
        thread.join();
    }

    libusb_exit(context);
}

void X360Receiver::run()
{
    Clock::time_point nextAttempt = Clock::now();

    while (!stopping)
    {
        if (!handle && Clock::now() >= nextAttempt)
        {
            if (!open())
            {
                nextAttempt = Clock::now() + RETRY_INTERVAL;
            }
        }

        timeval timeout = { 0, 100000 };
        libusb_handle_events_timeout_completed(context, &timeout, nullptr);

        runTasks();
        stopExpiredRumble();

        if (handle && lost)
        {
            Log::info("[x360] Receiver disconnected");

            close(false);

            nextAttempt = Clock::now() + RETRY_INTERVAL;
        }
    }

    close(true);
}

bool X360Receiver::open()
{
    libusb_device **list = nullptr;
    const ssize_t count = libusb_get_device_list(context, &list);

    libusb_device *device = nullptr;
    uint16_t productId = 0;

    for (ssize_t i = 0; i < count && !device; i++)
    {
        libusb_device_descriptor descriptor = {};

        if (libusb_get_device_descriptor(list[i], &descriptor) != LIBUSB_SUCCESS ||
            descriptor.idVendor != X360::VENDOR_ID)
        {
            continue;
        }

        for (uint16_t id : X360::PRODUCT_IDS)
        {
            if (descriptor.idProduct == id)
            {
                device = libusb_ref_device(list[i]);
                productId = id;
            }
        }
    }

    if (list)
    {
        libusb_free_device_list(list, 1);
    }

    if (!device)
    {
        // Unplugged while held elsewhere: nothing to say about it any more.
        reportBusy("");

        return false;
    }

    int error = libusb_open(device, &handle);
    libusb_unref_device(device);

    if (error != LIBUSB_SUCCESS)
    {
        handle = nullptr;
        reportBusy(exclusiveOwner(productId));

        return false;
    }

    libusb_config_descriptor *config = nullptr;

    if (libusb_get_config_descriptor(libusb_get_device(handle), 0, &config) != LIBUSB_SUCCESS)
    {
        libusb_close(handle);
        handle = nullptr;

        return false;
    }

    /*
     * Plugged in afresh and with no driver of its own, the receiver sits
     * unconfigured: macOS configures a device only for a driver that matches
     * it, and Steam is such a driver when it runs. Set only when not set
     * already - on macOS setting a configuration re-enumerates the device
     * even when nothing changes (see Dongle, which learnt that first).
     */
    int active = -1;
    libusb_get_configuration(handle, &active);

    if (active != config->bConfigurationValue)
    {
        error = libusb_set_configuration(handle, config->bConfigurationValue);

        if (error != LIBUSB_SUCCESS)
        {
            libusb_free_config_descriptor(config);
            libusb_close(handle);
            handle = nullptr;
            reportBusy(exclusiveOwner(productId));

            return false;
        }
    }

    // A controller slot: class ff, subclass 5d, protocol 81, one interrupt
    // endpoint each way. Protocol 82 is the slot's headset port.
    for (int i = 0; i < config->bNumInterfaces &&
                    slots.size() < static_cast<size_t>(X360::SLOT_COUNT); i++)
    {
        const libusb_interface_descriptor &alt = config->interface[i].altsetting[0];

        if (alt.bInterfaceClass != 0xff || alt.bInterfaceSubClass != 0x5d ||
            alt.bInterfaceProtocol != 0x81)
        {
            continue;
        }

        std::unique_ptr<Slot> slot(new Slot());
        slot->receiver = this;
        slot->number = static_cast<int>(slots.size()) + 1;
        slot->interface = alt.bInterfaceNumber;

        for (int e = 0; e < alt.bNumEndpoints; e++)
        {
            const libusb_endpoint_descriptor &endpoint = alt.endpoint[e];

            if ((endpoint.bmAttributes & 0x03) != LIBUSB_TRANSFER_TYPE_INTERRUPT)
            {
                continue;
            }

            if (endpoint.bEndpointAddress & LIBUSB_ENDPOINT_IN)
            {
                slot->in = endpoint.bEndpointAddress;
            }

            else
            {
                slot->out = endpoint.bEndpointAddress;
            }
        }

        if (slot->in && slot->out)
        {
            slots.push_back(std::move(slot));
        }
    }

    libusb_free_config_descriptor(config);

    for (size_t i = 0; i < slots.size(); i++)
    {
        error = libusb_claim_interface(handle, slots[i]->interface);

        if (error != LIBUSB_SUCCESS)
        {
            for (size_t k = 0; k < i; k++)
            {
                libusb_release_interface(handle, slots[k]->interface);
            }

            slots.clear();
            libusb_close(handle);
            handle = nullptr;
            reportBusy(exclusiveOwner(productId));

            return false;
        }
    }

    if (slots.empty())
    {
        Log::error("[x360] Receiver %04x:%04x has no controller slots",
                   X360::VENDOR_ID, productId);

        libusb_close(handle);
        handle = nullptr;

        return false;
    }

    reportBusy("");
    lost = false;

    for (std::unique_ptr<Slot> &slot : slots)
    {
        slot->read = libusb_alloc_transfer(0);

        libusb_fill_interrupt_transfer(slot->read, handle, slot->in,
                                       slot->buffer, X360::PACKET_SIZE,
                                       &X360Receiver::onRead, slot.get(), 0);

        slot->reading = libusb_submit_transfer(slot->read) == LIBUSB_SUCCESS;

        // Controllers that were on before we started never send a presence
        // packet of their own; asked, every slot answers.
        send(*slot, X360::presenceQuery());
    }

    Log::info("[x360] Xbox 360 receiver %04x:%04x ready, %zu slots",
              X360::VENDOR_ID, productId, slots.size());

    return true;
}

void X360Receiver::close(bool powerOff)
{
    if (!handle)
    {
        return;
    }

    for (std::unique_ptr<Slot> &slot : slots)
    {
        if (slot->present && powerOff)
        {
            send(*slot, X360::powerOff());
        }

        if (slot->present)
        {
            disconnect(*slot);
        }

        if (slot->reading)
        {
            libusb_cancel_transfer(slot->read);
        }
    }

    // The callbacks point into the slots: let every read report its
    // cancellation and every write finish before the slots go.
    const Clock::time_point deadline = Clock::now() + std::chrono::seconds(2);

    auto busy = [this]() {
        if (pendingWrites > 0)
        {
            return true;
        }

        for (const std::unique_ptr<Slot> &slot : slots)
        {
            if (slot->reading)
            {
                return true;
            }
        }

        return false;
    };

    while (busy() && Clock::now() < deadline)
    {
        timeval timeout = { 0, 100000 };
        libusb_handle_events_timeout_completed(context, &timeout, nullptr);
    }

    for (std::unique_ptr<Slot> &slot : slots)
    {
        if (!slot->reading)
        {
            libusb_free_transfer(slot->read);
        }

        libusb_release_interface(handle, slot->interface);
    }

    slots.clear();

    libusb_close(handle);
    handle = nullptr;
    lost = false;
}

void X360Receiver::reportBusy(const std::string &owner)
{
    if (owner == busyOwner)
    {
        return;
    }

    busyOwner = owner;

    if (owner.empty())
    {
        Status::setNotice("x360", "");

        return;
    }

    const std::string name = friendlyName(owner);

    Log::info("[x360] The Xbox 360 receiver is in use by %s (%s); trying again "
              "until it is free", name.c_str(), owner.c_str());

    Status::setNotice("x360", "Xbox 360 receiver in use by " + name);
}

void X360Receiver::post(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(taskMutex);

        tasks.push(std::move(task));
    }

    libusb_interrupt_event_handler(context);
}

void X360Receiver::runTasks()
{
    for (;;)
    {
        std::function<void()> task;

        {
            std::lock_guard<std::mutex> lock(taskMutex);

            if (tasks.empty())
            {
                return;
            }

            task = std::move(tasks.front());
            tasks.pop();
        }

        task();
    }
}

void X360Receiver::onRead(libusb_transfer *transfer)
{
    Slot &slot = *static_cast<Slot *>(transfer->user_data);
    X360Receiver &receiver = *slot.receiver;

    switch (transfer->status)
    {
        case LIBUSB_TRANSFER_COMPLETED:
            if (transfer->actual_length > 0)
            {
                receiver.process(slot, transfer->buffer, transfer->actual_length);
            }
            break;

        case LIBUSB_TRANSFER_CANCELLED:
            slot.reading = false;
            return;

        case LIBUSB_TRANSFER_TIMED_OUT:
            break;

        default:
            // Unplugged, or broken enough that opening it again is the fix.
            slot.reading = false;
            receiver.lost = true;
            return;
    }

    if (receiver.stopping || libusb_submit_transfer(transfer) != LIBUSB_SUCCESS)
    {
        slot.reading = false;
        receiver.lost = !receiver.stopping;
    }
}

void X360Receiver::onWrite(libusb_transfer *transfer)
{
    Slot &slot = *static_cast<Slot *>(transfer->user_data);

    slot.receiver->pendingWrites--;

    if (transfer->status == LIBUSB_TRANSFER_COMPLETED ||
        transfer->status == LIBUSB_TRANSFER_CANCELLED ||
        transfer->status == LIBUSB_TRANSFER_NO_DEVICE || slot.writeFailed)
    {
        return;
    }

    slot.writeFailed = true;

    /*
     * Measured: input arrives but every command times out when the receiver
     * sits behind some USB hubs (a dock's, here). The controller then works
     * without its ring light and without rumble. Said once, in the log.
     */
    Log::error("[x360] Commands to the controller in slot %d are not getting "
               "through (status %d). Is the receiver behind a USB hub or dock? "
               "Plugged into the Mac directly it works.", slot.number,
               transfer->status);
}

void X360Receiver::send(Slot &slot, const X360::Command &command)
{
    if (!handle)
    {
        return;
    }

    libusb_transfer *transfer = libusb_alloc_transfer(0);
    unsigned char *data = static_cast<unsigned char *>(std::malloc(X360::COMMAND_SIZE));

    std::memcpy(data, command.data(), X360::COMMAND_SIZE);

    libusb_fill_interrupt_transfer(transfer, handle, slot.out, data,
                                   X360::COMMAND_SIZE, &X360Receiver::onWrite,
                                   &slot, 1000);

    // libusb frees both when the transfer is done, whatever the outcome.
    transfer->flags = LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;

    if (libusb_submit_transfer(transfer) == LIBUSB_SUCCESS)
    {
        pendingWrites++;
    }

    else
    {
        libusb_free_transfer(transfer);
    }
}

void X360Receiver::process(Slot &slot, const uint8_t *data, int length)
{
    const size_t size = static_cast<size_t>(length);

    switch (X360::classify(data, size))
    {
        case X360::Kind::Presence:
            if (X360::present(data))
            {
                connect(slot);
            }

            else if (slot.present)
            {
                disconnect(slot);
            }
            break;

        case X360::Kind::Link:
            slot.identity = X360::identity(data, size);
            connect(slot);
            publish(slot);
            break;

        case X360::Kind::Input:
            // A controller that was on before we listened sends input first.
            connect(slot);

            if (X360::parseInput(data, size, slot.state))
            {
                publish(slot);
                slot.output->update(slot.state);
            }
            break;

        case X360::Kind::Other:
            break;
    }
}

void X360Receiver::connect(Slot &slot)
{
    if (slot.present)
    {
        return;
    }

    slot.present = true;
    slot.writeFailed = false;

    // Stops the ring spinning and shows the slot, as on a console.
    send(slot, X360::ringLight(slot.number - 1));

    Log::info("[x360] Controller connected to slot %d", slot.number);
}

void X360Receiver::disconnect(Slot &slot)
{
    slot.present = false;
    slot.generation++;
    slot.identity.clear();
    slot.state = GamepadState();
    slot.strong = slot.weak = 0;
    slot.rumbleTimed = false;

    if (slot.output)
    {
        slot.output.reset();

        Status::controllerDisconnected(statusKey(slot.number));
    }

    Log::info("[x360] Controller in slot %d disconnected", slot.number);
}

/*
 * The virtual pad, created once per connection: on the link packet, which
 * carries the controller's identity, or on the first input if that was
 * missed. The pad claims to be the same Xbox pad as the adapter's
 * controllers, so games see one kind of controller, whichever receiver it
 * came through.
 */
void X360Receiver::publish(Slot &slot)
{
    if (slot.output)
    {
        return;
    }

    slot.output = makeOutputDevice();

    const int number = slot.number;
    const unsigned generation = slot.generation;

    // Arrives on CoreHID's thread; queued, and dropped if the controller it
    // was meant for has left in the meantime.
    slot.output->setRumbleCallback([this, number, generation](const RumbleEffect &effect) {
        post([this, number, generation, effect]() {
            for (std::unique_ptr<Slot> &candidate : slots)
            {
                if (candidate->number == number &&
                    candidate->generation == generation && candidate->output)
                {
                    rumble(*candidate, effect);
                }
            }
        });
    });

    DeviceInfo info;
    info.vendorId = X360::VENDOR_ID;
    info.productId = 0x028e;
    info.version = 0x0114;
    info.name = "Xbox 360 Wireless Controller";
    info.serial = slot.identity.empty()
        ? "x360-slot-" + std::to_string(number)
        : "x360-" + slot.identity;

    slot.output->create(info);

    Status::controllerConnected(statusKey(number),
                                "Xbox 360 controller " + std::to_string(number));
}

/*
 * The 360 pad knows only "run the motors at this strength" - no duration,
 * no trigger motors. A request with a duration (macOS's Identify in System
 * Settings sends one) is stopped here when it is up; SDL sends 0xFF, "until
 * told otherwise", and stops it itself.
 */
void X360Receiver::rumble(Slot &slot, const RumbleEffect &effect)
{
    if (effect.enable & 0x02)
    {
        slot.strong = X360::motorFrom(effect.left);
    }

    if (effect.enable & 0x01)
    {
        slot.weak = X360::motorFrom(effect.right);
    }

    send(slot, X360::rumble(slot.strong, slot.weak));

    const bool running = slot.strong || slot.weak;

    slot.rumbleTimed = running && effect.duration != 0xff;

    if (slot.rumbleTimed)
    {
        // In 10 ms steps: `duration` on, `delay` off, `repeat` more times.
        const int steps = effect.duration * (effect.repeat + 1) +
                          effect.delay * effect.repeat;

        slot.rumbleUntil = Clock::now() + std::chrono::milliseconds(10 * steps);
    }
}

void X360Receiver::stopExpiredRumble()
{
    const Clock::time_point now = Clock::now();

    for (std::unique_ptr<Slot> &slot : slots)
    {
        if (slot->rumbleTimed && now >= slot->rumbleUntil)
        {
            slot->rumbleTimed = false;
            slot->strong = slot->weak = 0;

            send(*slot, X360::rumble(0, 0));
        }
    }
}
