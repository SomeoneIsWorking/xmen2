#include "x2_log.h"
/* DirectInput joystick state layout and object enumeration. */
#include "dinput_joystick.h"
#include "guest_memory.h"

#include "alchemy_controller_bridge.h"
#include "dinput_pad.h"
#include "directinput_controller_sample.h"
#include "guest_heap.h"
#include "joystick_neutral.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <string.h>

void dinput_joystick_state(int pad, int32_t lo, int32_t hi, uint32_t out,
                           uint32_t size) {
  X2DirectInputControllerSample sample;

  /* Latch SDL's current view ONCE, before reading the sixteen values below
     out of it. Without this every one of them reports what SDL happened to
     hold at startup, which is "nothing pressed" forever. */
  dinput_pad_refresh_state();

  if (!x2_joystick_write_neutral(guest_memory_pointer(out), size, lo, hi)) {
    x2_log_error("DINPUT8: a %u-byte joystick state is smaller than the "
                 "176 bytes DIJOYSTATE2 needs for axes, POVs and "
                 "buttons. Nothing is written.\n",
                 size);
    return;
  }
  if (!x2_directinput_controller_capture(pad, lo, hi, &sample)) {
    return;
  }
  x2_directinput_controller_write(
      &sample, (unsigned char *)guest_memory_pointer(out), size);
  x2_alchemy_controller_observe(pad, &sample, lo, hi);
}

static void object_guid(unsigned char guid[16], unsigned char low) {
  static const unsigned char REST[15] = {0x02, 0x6D, 0xA3, 0xF3, 0xC9,
                                         0xCF, 0x11, 0xBF, 0xC7, 0x44,
                                         0x45, 0x53, 0x54, 0x00, 0x00};
  guid[0] = low;
  memcpy(guid + 1, REST, sizeof REST);
}

#define DIDFT_ABSAXIS 0x00000002u
#define DIDFT_PSHBUTTON 0x00000004u
#define DIDFT_POV 0x00000010u
#define DIOBJ_BYTES 0x13Cu

static void enum_object(CPU *cpu, uint32_t callback, uint32_t context,
                        uint32_t buffer, unsigned char guid_low,
                        uint32_t offset, uint32_t type, const char *name,
                        int *stop) {
  CPU call;
  unsigned char guid[16];

  if (*stop)
    return;
  memset(guest_memory_pointer(buffer), 0, DIOBJ_BYTES);
  WR32(buffer, DIOBJ_BYTES);
  object_guid(guid, guid_low);
  memcpy(guest_memory_pointer(buffer + 4u), guid, sizeof guid);
  WR32(buffer + 0x14u, offset);
  WR32(buffer + 0x18u, type);
  WR32(buffer + 0x1cu, 0); /* no DIDOI_FFACTUATOR: force feedback absent */
  snprintf(guest_memory_pointer(buffer + 0x20u), 260, "%s", name);
  call = *cpu;
  call.reg[kX86pEsp] -= 8u;
  WR32(call.reg[kX86pEsp], buffer);
  WR32(call.reg[kX86pEsp] + 4u, context);
  x86_guest_call_args(&call, callback, 8u);
  if (call.reg[kX86pEax] == 0u)
    *stop = 1;
}

uint32_t dinput_joystick_enum_objects(CPU *cpu, int pad, uint32_t callback,
                                      uint32_t context, uint32_t filter) {
  static uint32_t buffer;
  static const struct {
    unsigned char low;
    const char *name;
  } AXES[6] = {{0xE0, "X Axis"},     {0xE1, "Y Axis"},
               {0xE2, "Z Axis"},     {0xF4, "X Rotation"},
               {0xF5, "Y Rotation"}, {0xE3, "Z Rotation"}};
  int stop = 0, i, buttons;

  if (!buffer && !(buffer = guest_malloc(DIOBJ_BYTES)))
    return 0x8007000Eu;
  for (i = 0; i < 6; i++) {
    uint32_t type = DIDFT_ABSAXIS | ((uint32_t)i << 8);
    if (filter && !(filter & DIDFT_ABSAXIS))
      break;
    enum_object(cpu, callback, context, buffer, AXES[i].low, (uint32_t)i * 4u,
                type, AXES[i].name, &stop);
  }
  if (!filter || (filter & DIDFT_POV))
    enum_object(cpu, callback, context, buffer, 0xF2, 32u, DIDFT_POV,
                "Hat Switch", &stop);
  buttons = dinput_pad_button_count(pad);
  if (!filter || (filter & DIDFT_PSHBUTTON))
    for (i = 0; i < buttons; i++) {
      char name[32];
      snprintf(name, sizeof name, "Button %d", i);
      enum_object(cpu, callback, context, buffer, 0xF0, 48u + (uint32_t)i,
                  DIDFT_PSHBUTTON | ((uint32_t)i << 8), name, &stop);
    }
  return 0;
}
