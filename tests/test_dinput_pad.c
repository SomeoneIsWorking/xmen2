/*
 * The gamepad inventory, against a virtual pad.
 *
 * This checks the layer that decides what the GAME sees: which pads exist,
 * what a stick reads in the range the game asked for, which DirectInput button
 * number each physical button is, and what the d-pad reports as a POV. Every
 * one of those is a place where a wrong answer looks like working input --
 * a stick scaled into the wrong range still moves, a button off by one still
 * responds, a POV that returns 0 for centred still points somewhere.
 *
 * THE NEGATIVE IS CHECKED FIRST, on purpose. With no pad attached this must
 * report zero pads and a CENTRED stick, and if it cannot do that then a later
 * "the stick reads centred" proves nothing at all -- it is what a broken
 * inventory returns too.
 */
#include "dinput_pad.h"

#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(c) do { assert(c); checks++; } while (0)

/* The range XMen2.exe sets on every axis (FUN_00628510). Using the game's own
   numbers rather than round ones is the point: this is the scale the values
   actually have to come back on. */
#define LO (-1000)
#define HI ( 1000)

static void test_no_pad(void)
{
    int32_t v;
    CHECK(dinput_pad_count() == 0);
    /* Centred is the MIDPOINT of the range, which for [-1000,1000] is 0 --
       and for DirectInput's own default [0,65535] would be 32767. A host that
       returned a literal 0 in that case would report a stick hard left. */
    v = dinput_pad_axis(0, DINPUT_PAD_AXIS_X, LO, HI);
    CHECK(v == 0);
    v = dinput_pad_axis(0, DINPUT_PAD_AXIS_X, 0, 65535);
    CHECK(v == 32767);
    CHECK(dinput_pad_pov(0) == 0xFFFFFFFFu);
    CHECK(dinput_pad_button(0, 0) == 0);
    CHECK(dinput_pad_name(0) == NULL);
    printf("no pad: 0 pads, axes centred in whatever range is asked for: ok\n");
}

int main(void)
{
    SDL_VirtualJoystickDesc desc;
    SDL_JoystickID jid, jid2;
    SDL_Joystick *joy;
    SDL_GUID g;
    char gs[64], map[512];
    unsigned char inst[16], inst2[16], prod[16];
    char persistent[64];
    int32_t v;

    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        printf("SKIP dinput_pad: SDL_Init(GAMEPAD): %s\n", SDL_GetError());
        return 77;
    }

    test_no_pad();

    /* Prompt selection follows SDL's physical-family classification, not the
       Xbox-shaped DirectInput layout we intentionally present for every pad. */
    CHECK(dinput_pad_type_uses_xbox_glyphs(SDL_GAMEPAD_TYPE_XBOX360) == 1);
    CHECK(dinput_pad_type_uses_xbox_glyphs(SDL_GAMEPAD_TYPE_XBOXONE) == 1);
    CHECK(dinput_pad_type_uses_xbox_glyphs(SDL_GAMEPAD_TYPE_PS5) == 0);
    CHECK(dinput_pad_type_uses_xbox_glyphs(SDL_GAMEPAD_TYPE_STANDARD) == 0);
    printf("prompt family: Xbox 360/One positive, PS5/standard negative: ok\n");

    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = 0x045e;
    desc.product_id = 0x028e;
    desc.name = "Virtual Xbox 360 Pad";
    desc.naxes = 6;
    desc.nbuttons = 11;
    desc.nhats = 1;
    jid = SDL_AttachVirtualJoystick(&desc);
    if (jid == 0) {
        printf("SKIP dinput_pad (virtual joystick): %s\n", SDL_GetError());
        return 77;
    }
    g = SDL_GetJoystickGUIDForID(jid);
    SDL_GUIDToString(g, gs, sizeof gs);
    snprintf(map, sizeof map,
             "%s,Virtual Pad,a:b0,b:b1,x:b2,y:b3,back:b4,start:b5,"
             "leftstick:b6,rightstick:b7,leftshoulder:b8,rightshoulder:b9,"
             "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,"
             "leftx:a0,lefty:a1,rightx:a2,righty:a3,"
             "lefttrigger:a4,righttrigger:a5,", gs);
    SDL_AddGamepadMapping(map);

    dinput_pad_refresh();
    CHECK(dinput_pad_count() == 1);
    CHECK(dinput_pad_name(0) != NULL);
    CHECK(dinput_pad_uses_xbox_glyphs(0) == 1);
    /* The identity the game keys its player slots on has to exist and the two
       GUIDs must differ -- an instance GUID equal to the product GUID would
       make two identical pads the same device. */
    CHECK(dinput_pad_instance_guid(0, inst) == 1);
    CHECK(dinput_pad_product_guid(0, prod) == 1);
    CHECK(memcmp(inst, prod, 16) != 0);
    /* And a CreateDevice for the GUID an enumeration handed out has to find
       its way back to this pad, or the game can see it and never open it. */
    CHECK(dinput_pad_for_guid(inst) == 0);
    CHECK(dinput_pad_persistent_id(0) != NULL);
    snprintf(persistent, sizeof persistent, "%s",
             dinput_pad_persistent_id(0));
    CHECK(dinput_pad_for_persistent_id(persistent) == 0);
    { unsigned char other[16]; memset(other, 0xAB, 16);
      CHECK(dinput_pad_for_guid(other) == -1); }
    printf("virtual pad: enumerated, identified, findable by GUID: ok\n");

    /* SDL gives identical models the same joystick GUID. That is a product
       identifier, not a DirectInput instance identifier: both units must
       enumerate and remain independently openable and assignable. */
    jid2 = SDL_AttachVirtualJoystick(&desc);
    CHECK(jid2 != 0);
    dinput_pad_refresh();
    CHECK(dinput_pad_count() == 2);
    CHECK(dinput_pad_instance_guid(1, inst2) == 1);
    CHECK(memcmp(inst, inst2, sizeof inst) != 0);
    CHECK(dinput_pad_for_guid(inst) == 0);
    CHECK(dinput_pad_for_guid(inst2) == 1);
    CHECK(strcmp(dinput_pad_persistent_id(0),
                 dinput_pad_persistent_id(1)) != 0);
    printf("identical pads: distinct live GUIDs and assignment identities: ok\n");
    SDL_DetachVirtualJoystick(jid2);
    dinput_pad_refresh();
    CHECK(dinput_pad_count() == 1);

    joy = SDL_OpenJoystick(jid);
    assert(joy);

    /* ---- axes, in the game's range ---- */
    SDL_SetJoystickVirtualAxis(joy, 0, 0);
    SDL_UpdateJoysticks();
    v = dinput_pad_axis(0, DINPUT_PAD_AXIS_X, LO, HI);
    CHECK(v > -60 && v < 60);                    /* centred */
    SDL_SetJoystickVirtualAxis(joy, 0, 32767);
    SDL_UpdateJoysticks();
    v = dinput_pad_axis(0, DINPUT_PAD_AXIS_X, LO, HI);
    CHECK(v > 900 && v <= HI);                   /* hard right, IN RANGE */
    SDL_SetJoystickVirtualAxis(joy, 0, -32768);
    SDL_UpdateJoysticks();
    v = dinput_pad_axis(0, DINPUT_PAD_AXIS_X, LO, HI);
    CHECK(v < -900 && v >= LO);                  /* hard left, IN RANGE */
    /* The same stick in DirectInput's default range, to prove the scaling is
       the caller's and not baked in. */
    v = dinput_pad_axis(0, DINPUT_PAD_AXIS_X, 0, 65535);
    CHECK(v < 3000);
    SDL_SetJoystickVirtualAxis(joy, 0, 0);
    SDL_UpdateJoysticks();
    printf("axes: scaled into the range the caller asks for, both ways: ok\n");

    /* ---- the triggers share Z, as they do on a real 360 pad ----
       The SCALE is checked, not just the sign. A trigger held alone has to
       reach the same extreme a fully deflected stick reaches, because the game
       puts one range on every axis and a binding either resolves to 1.0 or it
       does not. Checking `v > 100` passed a version that delivered HALF, and
       it also hid the fact that this test was never RELEASING the triggers:
       SDL maps a virtual joystick axis's WHOLE -32768..32767 travel onto the
       trigger's 0..32767, so a virtual axis left at 0 reads as a trigger held
       half down. `TRIG_UP` is what released means here. */
#define TRIG_UP   (-32768)
#define TRIG_DOWN ( 32767)
    SDL_SetJoystickVirtualAxis(joy, 5, TRIG_UP);
    SDL_SetJoystickVirtualAxis(joy, 4, TRIG_DOWN);   /* left alone */
    SDL_UpdateJoysticks();
    v = dinput_pad_axis(0, DINPUT_PAD_AXIS_Z, LO, HI);
    CHECK(v >= HI - 2);
    SDL_SetJoystickVirtualAxis(joy, 5, TRIG_DOWN);   /* and right: they cancel */
    SDL_UpdateJoysticks();
    v = dinput_pad_axis(0, DINPUT_PAD_AXIS_Z, LO, HI);
    CHECK(v > -100 && v < 100);
    SDL_SetJoystickVirtualAxis(joy, 4, TRIG_UP);     /* right alone */
    SDL_UpdateJoysticks();
    v = dinput_pad_axis(0, DINPUT_PAD_AXIS_Z, LO, HI);
    CHECK(v <= LO + 2);
    /* Half-pressed is half, so the scale above is a SCALE and not a clamp that
       reports the extreme for anything non-zero. */
    SDL_SetJoystickVirtualAxis(joy, 5, 0);
    SDL_UpdateJoysticks();
    v = dinput_pad_axis(0, DINPUT_PAD_AXIS_Z, LO, HI);
    CHECK(v < -400 && v > -600);
    SDL_SetJoystickVirtualAxis(joy, 5, TRIG_UP);
    SDL_UpdateJoysticks();
    v = dinput_pad_axis(0, DINPUT_PAD_AXIS_Z, LO, HI);
    CHECK(v == 0);                                   /* both released: centred */
    printf("triggers: one shared Z axis, left positive and right negative, "
           "each reaching full scale alone: ok\n");

    /* ---- buttons, in DirectInput's order for a 360 pad ---- */
    {
        static const struct { int sdl_button, di_button; const char *nm; } B[] = {
            { 0, 0, "A" }, { 1, 1, "B" }, { 2, 2, "X" }, { 3, 3, "Y" },
            { 8, 4, "LB" }, { 9, 5, "RB" }, { 4, 6, "Back" },
            { 5, 7, "Start" }, { 6, 8, "LS" }, { 7, 9, "RS" }
        };
        int i, j;
        for (i = 0; i < (int)(sizeof B / sizeof B[0]); i++) {
            SDL_SetJoystickVirtualButton(joy, B[i].sdl_button, true);
            SDL_UpdateJoysticks();
            CHECK(dinput_pad_button(0, B[i].di_button) == 1);
            /* And NOTHING ELSE is down. Without this, a mapping that reported
               every button pressed would pass every check above. */
            for (j = 0; j < 10; j++)
                if (j != B[i].di_button) CHECK(dinput_pad_button(0, j) == 0);
            SDL_SetJoystickVirtualButton(joy, B[i].sdl_button, false);
            SDL_UpdateJoysticks();
            CHECK(dinput_pad_button(0, B[i].di_button) == 0);
        }
    }
    printf("buttons: all 10 in the DirectInput order, one at a time: ok\n");

    /* ---- the d-pad as a POV, in hundredths of a degree ---- */
    {
        static const struct { unsigned char hat; uint32_t pov; } H[] = {
            { SDL_HAT_UP,        0 },     { SDL_HAT_RIGHTUP,   4500 },
            { SDL_HAT_RIGHT,     9000 },  { SDL_HAT_RIGHTDOWN, 13500 },
            { SDL_HAT_DOWN,      18000 }, { SDL_HAT_LEFTDOWN,  22500 },
            { SDL_HAT_LEFT,      27000 }, { SDL_HAT_LEFTUP,    31500 }
        };
        int i;
        for (i = 0; i < 8; i++) {
            SDL_SetJoystickVirtualHat(joy, 0, H[i].hat);
            SDL_UpdateJoysticks();
            CHECK(dinput_pad_pov(0) == H[i].pov);
        }
        SDL_SetJoystickVirtualHat(joy, 0, SDL_HAT_CENTERED);
        SDL_UpdateJoysticks();
        /* Centred is 0xFFFFFFFF and NOT 0 -- 0 is north, and a host that
           returned it would hold "up" for the whole run. */
        CHECK(dinput_pad_pov(0) == 0xFFFFFFFFu);
    }
    printf("d-pad: all 8 directions as POV angles, centred is not north: ok\n");

    /* ---- and it notices the pad going away ---- */
    SDL_CloseJoystick(joy);
    SDL_DetachVirtualJoystick(jid);
    SDL_UpdateJoysticks();
    dinput_pad_refresh();
    CHECK(dinput_pad_count() == 0);
    CHECK(dinput_pad_for_guid(inst) == -1);
    CHECK(dinput_pad_for_persistent_id(persistent) == -1);
    printf("unplug: the pad is gone and its GUID names nothing: ok\n");

    dinput_pad_report();
    printf("test_dinput_pad: %d checks passed\n", checks);
    SDL_Quit();
    return 0;
}
