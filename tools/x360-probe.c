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
 *   switch it on        a presence packet, and the ring shows its slot
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
#include <string.h>
#include <time.h>

enum { MAX_SLOTS = 4, PACKET = 32 };

/* The wireless receivers xpad knows; the first is the common one. */
static const uint16_t kProducts[] = { 0x0719, 0x0291, 0x02a9 };

struct Slot
{
    int interface;
    unsigned char in, out;
    unsigned char last[PACKET];
    int lastLength;
    int present;
};

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

/* Every command to a slot is 12 bytes on its interrupt OUT endpoint. */
static int command(libusb_device_handle *handle, const struct Slot *slot,
                   const unsigned char packet[12])
{
    int sent = 0;
    int error = libusb_interrupt_transfer(handle, slot->out,
                                          (unsigned char *)packet, 12,
                                          &sent, 500);

    if (error)
    {
        printf("    command to interface %d failed: %s\n",
               slot->interface, libusb_error_name(error));
    }

    return error;
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

int main(void)
{
    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    libusb_context *context = NULL;
    libusb_device_handle *handle = NULL;

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
     * One thread and short synchronous reads in turn: plenty for a probe,
     * and it keeps every libusb call on this thread.
     */
    while (running)
    {
        for (int i = 0; i < count && running; i++)
        {
            struct Slot *slot = &slots[i];
            unsigned char data[PACKET];
            int length = 0;

            int error = libusb_interrupt_transfer(handle, slot->in, data,
                                                  sizeof(data), &length, 20);

            if (error == LIBUSB_ERROR_TIMEOUT || length == 0)
            {
                continue;
            }

            if (error)
            {
                stamp();
                printf("slot %d: read failed: %s\n", i + 1, libusb_error_name(error));
                running = 0;
                break;
            }

            /* Presence: bit 3 of the first byte, then 0x80 = controller. */
            if ((data[0] & 0x08) && length >= 2)
            {
                const int present = (data[1] & 0x80) != 0;

                stamp();
                printf("slot %d: %s  ", i + 1,
                       present ? "controller connected   " : "controller disconnected");
                dump(data, length);

                if (present && !slot->present)
                {
                    light(handle, slot, i);
                }

                slot->present = present;
                continue;
            }

            /* Input: second byte 0x01. Print only when something changed. */
            if (data[1] == 0x01 && length >= 18)
            {
                if (length == slot->lastLength && memcmp(data, slot->last, length) == 0)
                {
                    continue;
                }

                const unsigned char *pad = data + 4;
                const int a = (pad[3] & 0x10) != 0, b = (pad[3] & 0x20) != 0;
                const int wasA = slot->lastLength && (slot->last[7] & 0x10);
                const int wasB = slot->lastLength && (slot->last[7] & 0x20);

                memcpy(slot->last, data, length);
                slot->lastLength = length;

                stamp();
                printf("slot %d: input  ", i + 1);
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

                continue;
            }

            /* Everything else — link, battery, whatever it turns out to be. */
            stamp();
            printf("slot %d: other  ", i + 1);
            dump(data, length);
        }

        fflush(stdout);
    }

    printf("\nstopping\n");

    for (int i = 0; i < count; i++)
    {
        rumble(handle, &slots[i], 0x00, 0x00);
        libusb_release_interface(handle, slots[i].interface);
    }

    libusb_close(handle);
    libusb_exit(context);

    return 0;
}
