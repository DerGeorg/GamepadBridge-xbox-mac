/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * x360-probe — what does the Xbox 360 Wireless Receiver actually send?
 *
 * The receiver's protocol is known from Linux's xpad driver, but "known" is
 * a claim until the bytes on this Mac say the same. This probe opens the
 * receiver, finds its four controller slots, and prints every packet — a
 * button's packet only when it differs from the last one, so pressing one
 * button gives one line.
 *
 * Try it on a controller:
 *   switch it on        a presence packet, and the ring shows its slot.
 *                       Still spinning? It is not paired with this receiver:
 *                       press the receiver's button, then the small connect
 *                       button on the controller's top edge.
 *   press A             rumbles the left (strong) motor for half a second
 *   press B             rumbles the right (weak) motor
 *   any other input     a line with the raw bytes
 *
 * Build and run (libusb from Homebrew), Ctrl-C to stop:
 *   clang tools/x360-probe.c $(pkg-config --cflags --libs libusb-1.0) \
 *         -o /tmp/x360-probe && /tmp/x360-probe
 */

#include <libusb.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { MAX_SLOTS = 4, PACKET = 32 };

/* The wireless receivers xpad knows; the first is the common one. */
static const uint16_t kProducts[] = { 0x0719, 0x0291, 0x02a9 };

struct Slot
{
    int number;
    int interface;
    unsigned char in, out;
    unsigned char last[PACKET];
    int lastLength;
    int present;
    int packets;
    unsigned char buffer[PACKET];
    struct libusb_transfer *read;
};

static libusb_device_handle *handle;

static volatile sig_atomic_t running = 1;

static void stop(int signal)
{
    (void)signal;
    running = 0;
}

static void stamp(void)
{
    char text[16];
    time_t now = time(NULL);

    strftime(text, sizeof(text), "%H:%M:%S", localtime(&now));
    printf("%s  ", text);
}

static void dump(const unsigned char *data, int length)
{
    for (int i = 0; i < length; i++)
    {
        printf("%02x ", data[i]);
    }
    printf("\n");
}

static void LIBUSB_CALL sent(struct libusb_transfer *transfer)
{
    if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
    {
        const struct Slot *slot = transfer->user_data;

        printf("    command to slot %d: %s\n", slot->number,
               libusb_error_name(transfer->status == LIBUSB_TRANSFER_TIMED_OUT
                                 ? LIBUSB_ERROR_TIMEOUT : LIBUSB_ERROR_IO));
    }
}

/*
 * Every command to a slot is 12 bytes on its interrupt OUT endpoint. Sent
 * asynchronously: the probe reacts to input inside libusb's callbacks, and a
 * synchronous transfer from a callback would wait on itself.
 */
static void command(libusb_device_handle *device, const struct Slot *slot,
                    const unsigned char packet[12])
{
    struct libusb_transfer *transfer = libusb_alloc_transfer(0);
    unsigned char *copy = malloc(12);

    memcpy(copy, packet, 12);
    libusb_fill_interrupt_transfer(transfer, device, slot->out, copy, 12,
                                   sent, (void *)slot, 1000);
    transfer->flags = LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;

    if (libusb_submit_transfer(transfer))
    {
        printf("    command to slot %d could not be sent\n", slot->number);
        libusb_free_transfer(transfer);
    }
}

static void rumble(libusb_device_handle *handle, const struct Slot *slot,
                   unsigned char strong, unsigned char weak)
{
    const unsigned char packet[12] = { 0x00, 0x01, 0x0f, 0xc0, 0x00,
                                       strong, weak };
    command(handle, slot, packet);
}

/*
 * Ring light, xpad's numbering: 2..5 blink and then light one quadrant, 1..4.
 * So a controller shows which slot it is in, the way it would on a console.
 */
static void light(libusb_device_handle *handle, const struct Slot *slot,
                  int number)
{
    const unsigned char packet[12] = { 0x00, 0x00, 0x08,
                                       (unsigned char)(0x40 + 2 + number) };
    command(handle, slot, packet);
}

static void process(struct Slot *slot, const unsigned char *data, int length)
{
    /*
     * The presence packet only comes when a controller connects, so a pad
     * that was on before the probe started never sends one. Light its ring
     * on the first packet instead, so the command gets tried either way.
     */
    if (slot->packets++ == 0 && !(data[0] & 0x08))
    {
        light(handle, slot, slot->number - 1);
    }

    /* Presence: bit 3 of the first byte, then 0x80 = controller. */
    if ((data[0] & 0x08) && length >= 2)
    {
        const int present = (data[1] & 0x80) != 0;

        stamp();
        printf("slot %d: %s  ", slot->number,
               present ? "controller connected   " : "controller disconnected");
        dump(data, length);

        if (present && !slot->present)
        {
            light(handle, slot, slot->number - 1);
        }

        slot->present = present;
        return;
    }

    /* Input: second byte 0x01. Print only when something changed. */
    if (data[1] == 0x01 && length >= 18)
    {
        if (length == slot->lastLength && memcmp(data, slot->last, length) == 0)
        {
            return;
        }

        const unsigned char *pad = data + 4;
        const int a = (pad[3] & 0x10) != 0, b = (pad[3] & 0x20) != 0;
        const int wasA = slot->lastLength && (slot->last[7] & 0x10);
        const int wasB = slot->lastLength && (slot->last[7] & 0x20);

        memcpy(slot->last, data, length);
        slot->lastLength = length;

        stamp();
        printf("slot %d: input  ", slot->number);
        dump(data, length);

        if (a && !wasA)
        {
            rumble(handle, slot, 0xff, 0x00);
        }
        else if (b && !wasB)
        {
            rumble(handle, slot, 0x00, 0xff);
        }
        else if ((!a && wasA) || (!b && wasB))
        {
            rumble(handle, slot, 0x00, 0x00);
        }

        return;
    }

    /* Everything else — link, battery, whatever it turns out to be. */
    stamp();
    printf("slot %d: other  ", slot->number);
    dump(data, length);
}

static void LIBUSB_CALL received(struct libusb_transfer *transfer)
{
    struct Slot *slot = transfer->user_data;

    if (transfer->status == LIBUSB_TRANSFER_CANCELLED)
    {
        return;
    }

    if (transfer->status == LIBUSB_TRANSFER_COMPLETED && transfer->actual_length > 0)
    {
        process(slot, transfer->buffer, transfer->actual_length);
    }
    else if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
    {
        stamp();
        printf("slot %d: read ended with status %d\n", slot->number, transfer->status);
    }

    if (running && libusb_submit_transfer(transfer))
    {
        printf("slot %d: cannot keep reading\n", slot->number);
    }
}

int main(void)
{
    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    libusb_context *context = NULL;

    if (libusb_init(&context))
    {
        printf("libusb_init failed\n");
        return 1;
    }

    for (size_t i = 0; !handle && i < sizeof(kProducts) / sizeof(kProducts[0]); i++)
    {
        handle = libusb_open_device_with_vid_pid(context, 0x045e, kProducts[i]);

        if (handle)
        {
            printf("receiver 045e:%04x\n", kProducts[i]);
        }
    }

    if (!handle)
    {
        printf("no Xbox 360 wireless receiver found\n");
        libusb_exit(context);
        return 1;
    }

    struct libusb_config_descriptor *config = NULL;

    if (libusb_get_active_config_descriptor(libusb_get_device(handle), &config))
    {
        printf("no configuration descriptor\n");
        return 1;
    }

    /*
     * A controller slot is an interface of class ff, subclass 5d, protocol
     * 81, with one interrupt endpoint each way. Protocol 82 is the slot's
     * headset, which this does not touch.
     */
    struct Slot slots[MAX_SLOTS];
    int count = 0;

    memset(slots, 0, sizeof(slots));

    for (int i = 0; i < config->bNumInterfaces && count < MAX_SLOTS; i++)
    {
        const struct libusb_interface_descriptor *alt = &config->interface[i].altsetting[0];

        printf("  interface %d: class %02x/%02x/%02x, %d endpoints\n",
               alt->bInterfaceNumber, alt->bInterfaceClass,
               alt->bInterfaceSubClass, alt->bInterfaceProtocol,
               alt->bNumEndpoints);

        if (alt->bInterfaceClass != 0xff || alt->bInterfaceSubClass != 0x5d ||
            alt->bInterfaceProtocol != 0x81)
        {
            continue;
        }

        struct Slot *slot = &slots[count];
        slot->number = count + 1;
        slot->interface = alt->bInterfaceNumber;

        for (int e = 0; e < alt->bNumEndpoints; e++)
        {
            const struct libusb_endpoint_descriptor *ep = &alt->endpoint[e];

            if ((ep->bmAttributes & 0x03) != LIBUSB_TRANSFER_TYPE_INTERRUPT)
            {
                continue;
            }

            if (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN)
            {
                slot->in = ep->bEndpointAddress;
            }
            else
            {
                slot->out = ep->bEndpointAddress;
            }
        }

        printf("    -> slot %d: in %02x, out %02x\n", count + 1, slot->in, slot->out);

        if (!slot->in || !slot->out)
        {
            continue;
        }

        int error = libusb_claim_interface(handle, slot->interface);

        if (error)
        {
            printf("    cannot claim interface %d: %s\n", slot->interface,
                   libusb_error_name(error));
            continue;
        }

        count++;
    }

    libusb_free_config_descriptor(config);

    printf("%d slot(s). Switch a controller on and press buttons; Ctrl-C to stop.\n\n",
           count);

    /* Ask every slot whether a controller is there. */
    for (int i = 0; i < count; i++)
    {
        const unsigned char query[12] = { 0x08, 0x00, 0x0f, 0xc0 };
        command(handle, &slots[i], query);
    }

    fflush(stdout);

    /*
     * One read per slot, always outstanding and never cancelled until the
     * end: a read that is cut off by a timeout can take a packet with it, and
     * the presence packet comes exactly once.
     */
    for (int i = 0; i < count; i++)
    {
        slots[i].read = libusb_alloc_transfer(0);
        libusb_fill_interrupt_transfer(slots[i].read, handle, slots[i].in,
                                       slots[i].buffer, PACKET, received,
                                       &slots[i], 0);

        if (libusb_submit_transfer(slots[i].read))
        {
            printf("slot %d: cannot start reading\n", slots[i].number);
        }
    }

    time_t lastReport = time(NULL);

    while (running)
    {
        struct timeval tv = { 0, 100000 };
        libusb_handle_events_timeout_completed(context, &tv, NULL);

        // A line every five seconds, so "nothing arrives" is visible as such.
        if (time(NULL) - lastReport >= 5)
        {
            lastReport = time(NULL);
            stamp();
            printf("packets so far:");
            for (int i = 0; i < count; i++)
            {
                printf("  slot %d: %d", slots[i].number, slots[i].packets);
            }
            printf("\n");
        }

        fflush(stdout);
    }

    for (int i = 0; i < count; i++)
    {
        libusb_cancel_transfer(slots[i].read);
    }

    printf("\nstopping\n");

    for (int i = 0; i < count; i++)
    {
        if (slots[i].present)
        {
            rumble(handle, &slots[i], 0x00, 0x00);
        }
    }

    // Let the cancellations and the last commands finish before letting go.
    for (int round = 0; round < 5; round++)
    {
        struct timeval tv = { 0, 100000 };
        libusb_handle_events_timeout_completed(context, &tv, NULL);
    }

    for (int i = 0; i < count; i++)
    {
        libusb_free_transfer(slots[i].read);
        libusb_release_interface(handle, slots[i].interface);
    }

    libusb_close(handle);
    libusb_exit(context);

    return 0;
}
