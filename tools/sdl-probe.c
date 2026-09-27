/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * sdl-probe — what does SDL make of this controller?
 *
 * The counterpart to gc-probe.m. GameController is one reader of the pad;
 * SDL is the other, and it is the one Steam and Wine/CrossOver build on. SDL
 * reads a Bluetooth Xbox pad with its own parser (SDL_hidapi_xboxone.c) and
 * ignores the descriptor, so a pad can be perfect in gc-probe and still come
 * out wrong here — the PlateUp! bug of 1.0.6 was exactly that.
 *
 * It also makes the pad rumble on request, so rumble can be tested without a
 * game that happens to use it:
 *
 *   A   left motor (strong, low frequency)
 *   B   right motor (weak, high frequency)
 *   X   both trigger motors
 *
 * Build and run (SDL3 from Homebrew):
 *   clang tools/sdl-probe.c $(pkg-config --cflags --libs sdl3) \
 *         -o /tmp/sdl-probe && /tmp/sdl-probe
 */

#include <SDL3/SDL.h>

#include <stdio.h>
#include <time.h>

static void stamp(void)
{
    char text[16];
    time_t now = time(NULL);

    strftime(text, sizeof(text), "%H:%M:%S", localtime(&now));
    printf("%s  ", text);
}

/*
 * Byte 14 of the GUID names the SDL driver that owns the device. 'h' is
 * HIDAPI, SDL's own parser — the path Steam and CrossOver take. Anything else
 * means SDL handed the pad to a different backend and this probe is not
 * testing what they see.
 */
static const char *driver(SDL_JoystickID id)
{
    SDL_GUID guid = SDL_GetJoystickGUIDForID(id);

    switch (guid.data[14])
    {
        case 'h': return "HIDAPI (what Steam and CrossOver use)";
        case 'm': return "GameController framework - NOT the HIDAPI path";
        case 'v': return "virtual";
        default:  return "other";
    }
}

static void opened(SDL_JoystickID id)
{
    SDL_Gamepad *pad = SDL_OpenGamepad(id);

    if (!pad)
    {
        printf("could not open gamepad %u: %s\n", (unsigned)id, SDL_GetError());
        return;
    }

    SDL_PropertiesID props = SDL_GetGamepadProperties(pad);

    printf("\n#%u %s\n", (unsigned)id, SDL_GetGamepadName(pad));
    printf("  driver   %s\n", driver(id));
    printf("  ids      %04x:%04x\n",
           SDL_GetGamepadVendor(pad), SDL_GetGamepadProduct(pad));
    printf("  serial   %s\n", SDL_GetGamepadSerial(pad) ?: "(none)");
    printf("  rumble   %s, trigger rumble %s\n",
           SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN,
                                  false) ? "yes" : "no",
           SDL_GetBooleanProperty(props,
                                  SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN,
                                  false) ? "yes" : "no");
    printf("  press A, B or X to rumble; every button is listed below\n");
    fflush(stdout);
}

static void rumble(SDL_JoystickID id, SDL_GamepadButton button)
{
    SDL_Gamepad *pad = SDL_GetGamepadFromID(id);
    bool sent = true;

    if (button == SDL_GAMEPAD_BUTTON_SOUTH)
    {
        sent = SDL_RumbleGamepad(pad, 0xFFFF, 0, 500);
    }

    else if (button == SDL_GAMEPAD_BUTTON_EAST)
    {
        sent = SDL_RumbleGamepad(pad, 0, 0xFFFF, 500);
    }

    else if (button == SDL_GAMEPAD_BUTTON_WEST)
    {
        sent = SDL_RumbleGamepadTriggers(pad, 0xFFFF, 0xFFFF, 500);
    }

    else
    {
        return;
    }

    if (!sent)
    {
        stamp();
        printf("#%u rumble refused by SDL: %s\n", (unsigned)id, SDL_GetError());
    }
}

int main(void)
{
    // A terminal program never has focus; without this SDL drops every event.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (!SDL_Init(SDL_INIT_GAMEPAD))
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    printf("SDL %d.%d.%d, waiting for gamepads (Ctrl-C to quit)\n",
           SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    fflush(stdout);

    SDL_Event event;

    for (;;)
    {
        if (!SDL_WaitEvent(&event))
        {
            continue;
        }

        switch (event.type)
        {
            case SDL_EVENT_QUIT:
                SDL_Quit();
                return 0;

            case SDL_EVENT_GAMEPAD_ADDED:
                opened(event.gdevice.which);
                break;

            case SDL_EVENT_GAMEPAD_REMOVED:
                stamp();
                printf("#%u removed\n", (unsigned)event.gdevice.which);
                break;

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            {
                const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                const SDL_GamepadButton button =
                    (SDL_GamepadButton)event.gbutton.button;

                stamp();
                printf("#%u %-14s %s\n", (unsigned)event.gbutton.which,
                       SDL_GetGamepadStringForButton(button),
                       down ? "DOWN" : "up");

                if (down)
                {
                    rumble(event.gbutton.which, button);
                }

                break;
            }
        }

        fflush(stdout);
    }
}
