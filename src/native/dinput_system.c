/*
 * SDL keyboard and mouse state at the DirectInput boundary.
 *
 * Device lifetime and COM semantics belong to dinput_device.c. Scripted test
 * input belongs to dinput_script.c. This module owns only the physical system
 * devices and the one authoritative SDL-scancode-to-DIK translation.
 */
#include "dinput_system.h"

#include "dinput_device.h"
#include "x86rt.h"

#include <stdio.h>
#include <string.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

/* SDL scancodes are USB HID usages; DIK values are PS/2 set-1 scancodes.
   There is no arithmetic conversion, so this explicit table is authoritative. */
static const struct { int sdl; unsigned char dik; } DIK_MAP[] = {
    { SDL_SCANCODE_ESCAPE, 0x01 },
    { SDL_SCANCODE_1, 0x02 }, { SDL_SCANCODE_2, 0x03 }, { SDL_SCANCODE_3, 0x04 },
    { SDL_SCANCODE_4, 0x05 }, { SDL_SCANCODE_5, 0x06 }, { SDL_SCANCODE_6, 0x07 },
    { SDL_SCANCODE_7, 0x08 }, { SDL_SCANCODE_8, 0x09 }, { SDL_SCANCODE_9, 0x0A },
    { SDL_SCANCODE_0, 0x0B },
    { SDL_SCANCODE_MINUS, 0x0C }, { SDL_SCANCODE_EQUALS, 0x0D },
    { SDL_SCANCODE_BACKSPACE, 0x0E }, { SDL_SCANCODE_TAB, 0x0F },
    { SDL_SCANCODE_Q, 0x10 }, { SDL_SCANCODE_W, 0x11 }, { SDL_SCANCODE_E, 0x12 },
    { SDL_SCANCODE_R, 0x13 }, { SDL_SCANCODE_T, 0x14 }, { SDL_SCANCODE_Y, 0x15 },
    { SDL_SCANCODE_U, 0x16 }, { SDL_SCANCODE_I, 0x17 }, { SDL_SCANCODE_O, 0x18 },
    { SDL_SCANCODE_P, 0x19 },
    { SDL_SCANCODE_LEFTBRACKET, 0x1A }, { SDL_SCANCODE_RIGHTBRACKET, 0x1B },
    { SDL_SCANCODE_RETURN, 0x1C }, { SDL_SCANCODE_LCTRL, 0x1D },
    { SDL_SCANCODE_A, 0x1E }, { SDL_SCANCODE_S, 0x1F }, { SDL_SCANCODE_D, 0x20 },
    { SDL_SCANCODE_F, 0x21 }, { SDL_SCANCODE_G, 0x22 }, { SDL_SCANCODE_H, 0x23 },
    { SDL_SCANCODE_J, 0x24 }, { SDL_SCANCODE_K, 0x25 }, { SDL_SCANCODE_L, 0x26 },
    { SDL_SCANCODE_SEMICOLON, 0x27 }, { SDL_SCANCODE_APOSTROPHE, 0x28 },
    { SDL_SCANCODE_GRAVE, 0x29 }, { SDL_SCANCODE_LSHIFT, 0x2A },
    { SDL_SCANCODE_BACKSLASH, 0x2B },
    { SDL_SCANCODE_Z, 0x2C }, { SDL_SCANCODE_X, 0x2D }, { SDL_SCANCODE_C, 0x2E },
    { SDL_SCANCODE_V, 0x2F }, { SDL_SCANCODE_B, 0x30 }, { SDL_SCANCODE_N, 0x31 },
    { SDL_SCANCODE_M, 0x32 },
    { SDL_SCANCODE_COMMA, 0x33 }, { SDL_SCANCODE_PERIOD, 0x34 },
    { SDL_SCANCODE_SLASH, 0x35 }, { SDL_SCANCODE_RSHIFT, 0x36 },
    { SDL_SCANCODE_KP_MULTIPLY, 0x37 }, { SDL_SCANCODE_LALT, 0x38 },
    { SDL_SCANCODE_SPACE, 0x39 }, { SDL_SCANCODE_CAPSLOCK, 0x3A },
    { SDL_SCANCODE_F1, 0x3B }, { SDL_SCANCODE_F2, 0x3C }, { SDL_SCANCODE_F3, 0x3D },
    { SDL_SCANCODE_F4, 0x3E }, { SDL_SCANCODE_F5, 0x3F }, { SDL_SCANCODE_F6, 0x40 },
    { SDL_SCANCODE_F7, 0x41 }, { SDL_SCANCODE_F8, 0x42 }, { SDL_SCANCODE_F9, 0x43 },
    { SDL_SCANCODE_F10, 0x44 },
    { SDL_SCANCODE_NUMLOCKCLEAR, 0x45 }, { SDL_SCANCODE_SCROLLLOCK, 0x46 },
    { SDL_SCANCODE_KP_7, 0x47 }, { SDL_SCANCODE_KP_8, 0x48 },
    { SDL_SCANCODE_KP_9, 0x49 }, { SDL_SCANCODE_KP_MINUS, 0x4A },
    { SDL_SCANCODE_KP_4, 0x4B }, { SDL_SCANCODE_KP_5, 0x4C },
    { SDL_SCANCODE_KP_6, 0x4D }, { SDL_SCANCODE_KP_PLUS, 0x4E },
    { SDL_SCANCODE_KP_1, 0x4F }, { SDL_SCANCODE_KP_2, 0x50 },
    { SDL_SCANCODE_KP_3, 0x51 }, { SDL_SCANCODE_KP_0, 0x52 },
    { SDL_SCANCODE_KP_PERIOD, 0x53 },
    { SDL_SCANCODE_F11, 0x57 }, { SDL_SCANCODE_F12, 0x58 },
    { SDL_SCANCODE_KP_ENTER, 0x9C }, { SDL_SCANCODE_RCTRL, 0x9D },
    { SDL_SCANCODE_KP_DIVIDE, 0xB5 }, { SDL_SCANCODE_RALT, 0xB8 },
    { SDL_SCANCODE_HOME, 0xC7 }, { SDL_SCANCODE_UP, 0xC8 },
    { SDL_SCANCODE_PAGEUP, 0xC9 }, { SDL_SCANCODE_LEFT, 0xCB },
    { SDL_SCANCODE_RIGHT, 0xCD }, { SDL_SCANCODE_END, 0xCF },
    { SDL_SCANCODE_DOWN, 0xD0 }, { SDL_SCANCODE_PAGEDOWN, 0xD1 },
    { SDL_SCANCODE_INSERT, 0xD2 }, { SDL_SCANCODE_DELETE, 0xD3 },
    { SDL_SCANCODE_LGUI, 0xDB }, { SDL_SCANCODE_RGUI, 0xDC },
    { SDL_SCANCODE_APPLICATION, 0xDD }
};
#define DIK_MAP_N ((int)(sizeof DIK_MAP / sizeof DIK_MAP[0]))
#endif

static unsigned long g_blind_reads;

int dinput_system_available(void)
{
#ifdef X2_WITH_SDL
    return SDL_WasInit(SDL_INIT_VIDEO) != 0;
#else
    return 0;
#endif
}

unsigned long dinput_system_blind_reads(void)
{
    return g_blind_reads;
}

static void say_blind(const char *what)
{
    if (g_blind_reads++) return;
    fprintf(stderr,
            "DINPUT8: the %s state was read with no SDL video subsystem up, so "
            "it reads as NOTHING PRESSED.\n"
            "  That is indistinguishable from a working device nobody is "
            "touching, which is why it is said here rather than left as a "
            "block of zeros.\n"
            "  Reported once; the total is in the exit report.\n", what);
}

unsigned char dinput_system_dik(int scancode)
{
#ifdef X2_WITH_SDL
    int i;
    for (i = 0; i < DIK_MAP_N; i++)
        if (DIK_MAP[i].sdl == scancode) return DIK_MAP[i].dik;
#else
    (void)scancode;
#endif
    return 0;
}

void dinput_system_keyboard_state(uint32_t out, uint32_t size)
{
    memset((void *)(uintptr_t)out, 0, size);
    if (!dinput_system_available()) { say_blind("keyboard"); return; }
#ifdef X2_WITH_SDL
    {
        int nkeys = 0, i;
        const bool *keys;
        SDL_PumpEvents();
        keys = SDL_GetKeyboardState(&nkeys);
        if (!keys) { say_blind("keyboard"); return; }
        for (i = 0; i < DIK_MAP_N; i++) {
            if (DIK_MAP[i].sdl >= nkeys || !keys[DIK_MAP[i].sdl]) continue;
            if ((uint32_t)DIK_MAP[i].dik >= size) continue;
            *((unsigned char *)(uintptr_t)out + DIK_MAP[i].dik) = 0x80;
        }
    }
#endif
}

void dinput_system_mouse_state(uint32_t out, uint32_t size)
{
    memset((void *)(uintptr_t)out, 0, size);
    if (!dinput_system_available()) { say_blind("mouse"); return; }
#ifdef X2_WITH_SDL
    {
        float dx = 0.0f, dy = 0.0f;
        SDL_MouseButtonFlags buttons;
        uint32_t count = size > 12u ? size - 12u : 0u;
        SDL_PumpEvents();
        buttons = SDL_GetRelativeMouseState(&dx, &dy);
        if (size >= 4u) WR32(out, (uint32_t)(int32_t)dx);
        if (size >= 8u) WR32(out + 4u, (uint32_t)(int32_t)dy);
        if (count > 0u && (buttons & SDL_BUTTON_LMASK))
            *((unsigned char *)(uintptr_t)out + 12) = 0x80;
        if (count > 1u && (buttons & SDL_BUTTON_RMASK))
            *((unsigned char *)(uintptr_t)out + 13) = 0x80;
        if (count > 2u && (buttons & SDL_BUTTON_MMASK))
            *((unsigned char *)(uintptr_t)out + 14) = 0x80;
    }
#endif
}

/* The system GUIDs are shared by DirectInput 7 and 8. */
static const unsigned char GUID_SYS_KEYBOARD[16] = {
    0x61,0x2B,0x1D,0x6F, 0xA0,0xD5, 0xCF,0x11,
    0xBF,0xC7, 0x44,0x45,0x53,0x54,0x00,0x00
};
static const unsigned char GUID_SYS_MOUSE[16] = {
    0x60,0x2B,0x1D,0x6F, 0xA0,0xD5, 0xCF,0x11,
    0xBF,0xC7, 0x44,0x45,0x53,0x54,0x00,0x00
};

int dinput_guid_kind(uint32_t guid)
{
    if (!guid) return 0;
    if (memcmp((const void *)(uintptr_t)guid, GUID_SYS_KEYBOARD, 16) == 0)
        return DINPUT_DEV_KEYBOARD;
    if (memcmp((const void *)(uintptr_t)guid, GUID_SYS_MOUSE, 16) == 0)
        return DINPUT_DEV_MOUSE;
    return 0;
}

const unsigned char *dinput_guid_of(int kind)
{
    return kind == DINPUT_DEV_KEYBOARD ? GUID_SYS_KEYBOARD
         : kind == DINPUT_DEV_MOUSE ? GUID_SYS_MOUSE : NULL;
}
