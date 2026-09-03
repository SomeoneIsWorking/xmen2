/*
 * IDirectInputDevice8 for the system keyboard and mouse, backed by SDL3.
 *
 * The game asks for these two BY FIXED GUID -- GUID_SysKeyboard and
 * GUID_SysMouse, read straight out of XMen2.exe at 0x6a15e4 and 0x6a15f4 -- so
 * they need no enumeration and no device list. That is why they are the first
 * devices this host implements: they are the ones the run actually stops on
 * (issue #32), and they are reachable without solving the joystick enumeration
 * that pads will need.
 *
 * The sequence the exe performs, read from FUN_00628e20:
 *
 *     CreateDevice(GUID_SysKeyboard, &dev, NULL)
 *     dev->SetDataFormat(c_dfDIKeyboard)        [vtable + 0x2c]  slot 11
 *     dev->SetCooperativeLevel(hwnd, 0x16)      [vtable + 0x34]  slot 13
 *     dev->Acquire()                            [vtable + 0x1c]  slot 7
 *
 * and the same for the mouse with c_dfDIMouse and level 6. Those offsets are
 * what fix the slot numbering below; it is not taken from a header.
 *
 * THE STATE SIZE IS NOT ASSUMED. SetDataFormat is handed the game's own
 * DIDATAFORMAT and this reads dwDataSize out of it, so GetDeviceState fills
 * exactly what the caller's format says -- measured, the mouse format is 20
 * bytes over 11 objects, which is DIMOUSESTATE2 (8 buttons) and NOT the
 * 16-byte DIMOUSESTATE the name c_dfDIMouse suggests. Hardcoding 16 would have
 * written short and left the last four buttons as whatever was on the stack.
 *
 * WHAT IS HONEST ABOUT THE NEGATIVE: with no SDL video subsystem there is no
 * keyboard to read, and every key would read as up -- which is exactly what a
 * working keyboard with nothing pressed looks like. That case says so once, by
 * name, and is counted, rather than returning a quiet block of zeros.
 */
#include "dinput_device.h"
#include "dinput_device_internal.h"
#include "dinput_device_registry.h"
#include "dinput_joystick.h"
#include "dinput_pad.h"
#include "dinput_system.h"
#include "guest_heap.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define A(i) RD32(C->esp + 4u + (uint32_t)(i) * 4u)
#define THIS A(0)

static void ret_com(CPU *C, uint32_t hr, int nargs) {
  dinput_device_return(C, hr, nargs);
}

#define S_OK 0x00000000u
#define S_FALSE 0x00000001u
#define DI_NOEFFECT 0x00000001u
#define DIERR_INVALIDPARAM 0x80070057u
#define DIERR_NOTACQUIRED 0x8007000Cu
#define DIERR_INPUTLOST 0x8007001Eu

/*
 * IDirectInputDevice8's vtable. The four the game dispatches through are
 * pinned by the offsets above; the rest follow DirectInput 8's published
 * order. A wrong entry here would silently run the wrong method, so this is
 * the one place the numbering is written down.
 */
enum {
  VT_QueryInterface,
  VT_AddRef,
  VT_Release,
  VT_GetCapabilities,
  VT_EnumObjects,
  VT_GetProperty,
  VT_SetProperty,
  VT_Acquire,
  VT_Unacquire,
  VT_GetDeviceState,
  VT_GetDeviceData,
  VT_SetDataFormat,
  VT_SetEventNotification,
  VT_SetCooperativeLevel,
  VT_GetObjectInfo,
  VT_GetDeviceInfo,
  VT_RunControlPanel,
  VT_Initialize,
  VT_CreateEffect,
  VT_EnumEffects,
  VT_GetEffectInfo,
  VT_GetForceFeedbackState,
  VT_SendForceFeedbackCommand,
  VT_EnumCreatedEffectObjects,
  VT_Escape,
  VT_Poll,
  VT_SendDeviceData,
  VT_EnumEffectsInFile,
  VT_WriteEffectToFile,
  VT_BuildActionMap,
  VT_SetActionMap,
  VT_GetImageInfo,
  VT_COUNT
};

static const char *const VT_NAME[VT_COUNT] = {"QueryInterface",
                                              "AddRef",
                                              "Release",
                                              "GetCapabilities",
                                              "EnumObjects",
                                              "GetProperty",
                                              "SetProperty",
                                              "Acquire",
                                              "Unacquire",
                                              "GetDeviceState",
                                              "GetDeviceData",
                                              "SetDataFormat",
                                              "SetEventNotification",
                                              "SetCooperativeLevel",
                                              "GetObjectInfo",
                                              "GetDeviceInfo",
                                              "RunControlPanel",
                                              "Initialize",
                                              "CreateEffect",
                                              "EnumEffects",
                                              "GetEffectInfo",
                                              "GetForceFeedbackState",
                                              "SendForceFeedbackCommand",
                                              "EnumCreatedEffectObjects",
                                              "Escape",
                                              "Poll",
                                              "SendDeviceData",
                                              "EnumEffectsInFile",
                                              "WriteEffectToFile",
                                              "BuildActionMap",
                                              "SetActionMap",
                                              "GetImageInfo"};

typedef DInputDevice Device;

static uint32_t g_vtable;

static Device *dev_of(uint32_t guest) {
  return dinput_device_registry_find(guest);
}

static const char *kind_name(DInputDeviceKind k) {
  return k == DINPUT_DEV_KEYBOARD   ? "keyboard"
         : k == DINPUT_DEV_MOUSE    ? "mouse"
         : k == DINPUT_DEV_JOYSTICK ? "gamepad"
                                    : "(unknown)";
}

static int pad_of(Device *device) { return dinput_device_pad(device); }

/* State translation and joystick metadata have focused module owners. */

static void m_EnumObjects(CPU *C) {
  Device *d = dev_of(THIS);
  uint32_t callback = A(1), context = A(2), filter = A(3);

  if (!d || !callback) {
    ret_com(C, DIERR_INVALIDPARAM, 3);
    return;
  }
  if (d->kind != DINPUT_DEV_JOYSTICK) {
    fprintf(stderr,
            "DINPUT8: EnumObjects on the %s, which this host does "
            "not describe object by object. Nothing is offered, "
            "and that is reported rather than passed off as an "
            "empty device.\n",
            kind_name(d->kind));
    ret_com(C, S_OK, 3);
    return;
  }
  ret_com(C,
          dinput_joystick_enum_objects(C, pad_of(d), callback, context, filter),
          3);
}

static void m_QueryInterface(CPU *C) {
  uint32_t ppv = A(2);
  if (ppv)
    WR32(ppv, THIS);
  ret_com(C, S_OK, 2);
}

static void m_AddRef(CPU *C) {
  Device *d = dev_of(THIS);
  uint32_t n = d ? ++d->refs : 1u;
  C->eax = n;
  C->esp += 4u + 4u;
}

static void m_Release(CPU *C) {
  Device *d = dev_of(THIS);
  uint32_t n = 0;
  if (d && d->refs)
    n = --d->refs;
  /* Not destroyed at zero: the two system devices live for the process and
     more than one caller holds each. Freeing would hand a survivor a
     dangling vtable, and the fault would look like an input bug. */
  C->eax = n;
  C->esp += 4u + 4u;
}

static void m_SetDataFormat(CPU *C) {
  /* (this, lpdf) -- the caller's own DIDATAFORMAT:
     dwSize, dwObjSize, dwFlags, dwDataSize, dwNumObjs, rgodf. */
  Device *d = dev_of(THIS);
  uint32_t df = A(1);
  if (!d || !df) {
    ret_com(C, DIERR_INVALIDPARAM, 1);
    return;
  }
  d->data_size = RD32(df + 12u);
  if (!d->data_size) {
    fprintf(stderr,
            "DINPUT8: SetDataFormat on the %s declared a zero-byte "
            "state. Refusing: GetDeviceState would then fill "
            "nothing and the game would read its own stack.\n",
            kind_name(d->kind));
    ret_com(C, DIERR_INVALIDPARAM, 1);
    return;
  }
  fprintf(stderr,
          "DINPUT8: the %s data format is %u byte(s) over %u "
          "object(s).\n",
          kind_name(d->kind), d->data_size, RD32(df + 16u));
  ret_com(C, S_OK, 1);
}

static void m_SetCooperativeLevel(CPU *C) {
  /* (this, hwnd, dwFlags). Nothing here can honour exclusivity -- SDL owns
     the window and this host does not steal the device from the desktop --
     and the game does not ask for it: the levels it passes are 0x16 and 6,
     both NONEXCLUSIVE. An EXCLUSIVE request would be a behaviour difference
     worth naming, so it is. */
  Device *d = dev_of(THIS);
  uint32_t flags = A(2);
  if (d)
    d->coop = flags;
  if (flags & 0x1u)
    fprintf(stderr,
            "DINPUT8: SetCooperativeLevel asked for EXCLUSIVE "
            "access to the %s (flags 0x%x); this host cannot take "
            "the device from the desktop, so it stays shared.\n",
            d ? kind_name(d->kind) : "device", flags);
  ret_com(C, S_OK, 2);
}

static void m_Acquire(CPU *C) {
  Device *d = dev_of(THIS);
  if (!d) {
    ret_com(C, DIERR_INVALIDPARAM, 0);
    return;
  }
  d->n_acquire++;
  if (d->kind == DINPUT_DEV_JOYSTICK && pad_of(d) < 0) {
    /* Acquiring a pad that is not plugged in must FAIL. Succeeding would
       make the next Poll the thing that fails instead, and the game would
       alternate between the two for the rest of the run believing it had
       a controller. Windows answers DIERR_INPUTLOST here too. */
    d->n_acquire_fail++;
    ret_com(C, DIERR_INPUTLOST, 0);
    return;
  }
  if (d->acquired) {
    ret_com(C, S_FALSE, 0);
    return;
  } /* already acquired */
  if (!d->data_size) {
    /* Real DirectInput refuses this, and so must we: the size the state
       will be written at is not known yet. */
    d->n_acquire_fail++;
    fprintf(stderr, "DINPUT8: Acquire on the %s before SetDataFormat.\n",
            kind_name(d->kind));
    ret_com(C, DIERR_INVALIDPARAM, 0);
    return;
  }
  d->acquired = 1;
  ret_com(C, S_OK, 0);
}

static void m_Unacquire(CPU *C) {
  Device *d = dev_of(THIS);
  if (d)
    d->acquired = 0;
  ret_com(C, S_OK, 0);
}

static void m_GetDeviceState(CPU *C) {
  dinput_device_get_state(C, dev_of(THIS));
}

static void m_GetDeviceData(CPU *C) {
  /* (this, cbObjectData, rgdod, pdwInOut, dwFlags) -- BUFFERED input, which
     needs DIPROP_BUFFERSIZE to have been set. It has not been, so DirectInput
     itself would fail here; reporting zero events without saying so would
     look like an idle frame forever. */
  Device *d = dev_of(THIS);
  uint32_t inout = A(3);
  static unsigned long told;
  if (!told++)
    fprintf(stderr,
            "DINPUT8: GetDeviceData (buffered input) on the %s -- "
            "no buffer was ever configured, so this reports ZERO "
            "events, every time. If the game relies on buffered "
            "keys rather than GetDeviceState, that is the next "
            "work item.\n",
            d ? kind_name(d->kind) : "device");
  if (inout)
    WR32(inout, 0);
  ret_com(C, S_OK, 5);
}

static void m_GetCapabilities(CPU *C) {
  /* (this, lpDIDevCaps): dwSize, dwFlags, dwDevType, dwAxes, dwButtons,
     dwPOVs, then force-feedback timings. */
  Device *d = dev_of(THIS);
  uint32_t caps = A(1);
  if (!caps || !d) {
    ret_com(C, DIERR_INVALIDPARAM, 1);
    return;
  }
  WR32(caps + 4u, 0x00000001u); /* DIDC_ATTACHED */
  if (d->kind == DINPUT_DEV_KEYBOARD) {
    WR32(caps + 8u, 0x00000103u); /* KEYBOARD | HID-less */
    WR32(caps + 12u, 0);          /* axes */
    WR32(caps + 16u, 256);        /* buttons */
  } else if (d->kind == DINPUT_DEV_JOYSTICK) {
    /* DI8DEVTYPE_GAMEPAD (0x15) with subtype DI8DEVTYPEGAMEPAD_STANDARD
       (1) in the second byte. A subtype of zero is not "unspecified" to a
       caller that switches on it. */
    WR32(caps + 8u, 0x00000115u);
    WR32(caps + 12u, 6); /* axes */
    WR32(caps + 16u, (uint32_t)dinput_pad_button_count(pad_of(d)));
    WR32(caps + 20u, 1); /* one POV: the d-pad */
    ret_com(C, S_OK, 1);
    return;
  } else {
    WR32(caps + 8u, 0x00000102u); /* MOUSE */
    WR32(caps + 12u, 3);
    WR32(caps + 16u, 8);
  }
  WR32(caps + 20u, 0); /* POVs */
  ret_com(C, S_OK, 1);
}

static void m_SetProperty(CPU *C) {
  /*
   * (this, rguidProp, pdiph). Accepted and IGNORED is not a safe default in
   * general -- DIPROP_BUFFERSIZE changes what GetDeviceData must do, and
   * DIPROP_AXISMODE changes whether the mouse axes are absolute -- so the
   * ones that would change behaviour are named when they arrive rather than
   * swallowed. rguidProp is a small integer cast to a pointer for the
   * predefined properties, which is what makes this readable at all.
   */
  uint32_t prop = A(1), ph = A(2);
  Device *d = dev_of(THIS);
  static unsigned long told;
  /*
   * DIPROP_RANGE (#4) is HONOURED, because the game depends on it: its axis
   * callback (XMen2.exe FUN_00628510) sets every axis to [-1000, +1000], and
   * a host that ignored it while returning DirectInput's default 0..65535
   * would report both sticks jammed hard over on every frame.
   *
   * DIPROPRANGE is a DIPROPHEADER {dwSize, dwHeaderSize, dwObj, dwHow}
   * followed by lMin and lMax.
   */
  if (prop == 4u && ph && d && d->kind == DINPUT_DEV_JOYSTICK) {
    int32_t lo = (int32_t)RD32(ph + 16u), hi = (int32_t)RD32(ph + 20u);
    if (hi > lo) {
      if (!d->range_set || d->axis_lo != lo || d->axis_hi != hi)
        fprintf(stderr,
                "DINPUT8: gamepad %d axis range set to "
                "[%d, %d] by the game; every axis it reads is "
                "scaled into that.\n",
                pad_of(d), lo, hi);
      d->axis_lo = lo;
      d->axis_hi = hi;
      d->range_set = 1;
    } else {
      fprintf(stderr,
              "DINPUT8: SetProperty(DIPROP_RANGE) with lMin %d "
              "not below lMax %d. Refused rather than stored: an "
              "inverted range makes every axis read backwards.\n",
              lo, hi);
      ret_com(C, DIERR_INVALIDPARAM, 2);
      return;
    }
    ret_com(C, S_OK, 2);
    return;
  }
  if (prop < 0x10000u && !told++)
    fprintf(stderr,
            "DINPUT8: SetProperty(#%u) is accepted and has NO "
            "effect here. Buffer size (#1) and axis mode (#2) would "
            "change what GetDeviceData and GetDeviceState must "
            "return; see src/native/dinput_device.c.\n",
            prop);
  ret_com(C, S_OK, 2);
}

static void m_Poll(CPU *C) {
  /*
   * FAILING HERE IS HOW THE GAME LEARNS TO ACQUIRE, so this must not be
   * generously successful.
   *
   * XMen2.exe's per-frame input update (FUN_006285c0, the loop at
   * 0x006287f0) is, for each of its ten device slots:
   *
   *     Poll();  if (hr < 0) { Acquire(); ... }  else GetDeviceState(0x110);
   *
   * -- so Acquire is reached ONLY down the failure branch. A host whose Poll
   * always returned S_OK left the pad forever unacquired: 6,260 Polls, 0
   * Acquires and 0 state reads in a run, with every step of the setup
   * looking correct. Real DirectInput answers DIERR_NOTACQUIRED, and that is
   * exactly the answer the game is written against.
   *
   * Once acquired there is genuinely nothing to do -- the state is read live
   * from SDL at GetDeviceState. A pad still answers DI_OK rather than
   * DI_NOEFFECT, because Windows does and a caller may treat DI_NOEFFECT as
   * "this device needs no polling".
   */
  Device *d = dev_of(THIS);
  if (!d) {
    ret_com(C, DIERR_INVALIDPARAM, 0);
    return;
  }
  d->n_poll++;
  if (d->kind == DINPUT_DEV_JOYSTICK && pad_of(d) < 0) {
    d->acquired = 0;
    ret_com(C, DIERR_INPUTLOST, 0); /* unplugged; see GetDeviceState */
    return;
  }
  if (!d->acquired) {
    ret_com(C, DIERR_NOTACQUIRED, 0);
    return;
  }
  ret_com(C, d->kind == DINPUT_DEV_JOYSTICK ? S_OK : DI_NOEFFECT, 0);
}

static void m_unimplemented(CPU *C) {
  const char *nm = (const char *)x86_callback_ctx();
  fprintf(stderr,
          "\n*** DINPUT8: IDirectInputDevice8::%s was called, and is not "
          "implemented.\n"
          "    The keyboard and mouse are served; this is the method the "
          "game wants NEXT, and that name IS the work item.\n"
          "    See src/native/dinput_device.c and issue #32.\n",
          nm ? nm : "(unknown slot)");
  fflush(stderr);
  x86_diag_dump();
  abort();
  (void)C;
}

/* ---- construction ------------------------------------------------------ */

static void build_vtable(void) {
  static void (*const impl[VT_COUNT])(CPU *) = {
      m_QueryInterface,
      m_AddRef,
      m_Release,
      m_GetCapabilities,
      m_EnumObjects,
      NULL, /* GetProperty */
      m_SetProperty,
      m_Acquire,
      m_Unacquire,
      m_GetDeviceState,
      m_GetDeviceData,
      m_SetDataFormat,
      NULL, /* SetEventNotification */
      m_SetCooperativeLevel,
      NULL, /* GetObjectInfo */
      NULL, /* GetDeviceInfo */
      NULL, /* RunControlPanel */
      NULL, /* Initialize */
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL, /* effects, Escape */
      m_Poll,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL /* the DX8 tail */
  };
  int k;
  if (g_vtable)
    return;
  g_vtable = guest_malloc(VT_COUNT * 4u);
  if (!g_vtable) {
    fprintf(stderr, "DINPUT8: no guest memory for the device vtable\n");
    abort();
  }
  for (k = 0; k < VT_COUNT; k++)
    WR32(g_vtable + (uint32_t)k * 4u,
         x86_native_callback(impl[k] ? impl[k] : m_unimplemented,
                             "IDirectInputDevice8", VT_NAME[k],
                             (void *)VT_NAME[k]));
}

static uint32_t device_alloc(DInputDeviceKind kind,
                             const unsigned char pad_guid[16]);

uint32_t dinput_device_new(DInputDeviceKind kind) {
  Device *device;

  /* One object per kind, for the whole process: the game creates the system
     keyboard once and caches it, and handing out a second would give the two
     holders different acquire states. Joysticks are the exception and go
     through dinput_device_new_pad -- there the whole point is one object per
     pad, and collapsing them would give four players one controller. */
  device = dinput_device_registry_find_system(kind);
  if (device) {
    device->refs++;
    return device->guest;
  }
  return device_alloc(kind, NULL);
}

/* One IDirectInputDevice8 per live instance GUID. The game creates one per
   player and keeps them in a ten-slot table keyed on that GUID, so two calls
   for the same instance share acquire state. A later device in the same host
   inventory slot gets a new object and cannot revive the disconnected one. */
uint32_t dinput_device_new_pad(const unsigned char guid[16]) {
  Device *device;
  if (!guid || dinput_pad_for_guid(guid) < 0)
    return 0;
  device = dinput_device_registry_find_controller(guid);
  if (device) {
    device->refs++;
    return device->guest;
  }
  return device_alloc(DINPUT_DEV_JOYSTICK, guid);
}

static uint32_t device_alloc(DInputDeviceKind kind,
                             const unsigned char pad_guid[16]) {
  Device *d;
  uint32_t obj;
  build_vtable();
  obj = guest_malloc(8u);
  if (!obj) {
    fprintf(stderr, "DINPUT8: no guest memory for a device\n");
    return 0;
  }
  WR32(obj + 0u, g_vtable);
  WR32(obj + 4u, 0);
  d = dinput_device_registry_append();
  if (!d) {
    fprintf(stderr, "DINPUT8: no host memory for another device\n");
    return 0;
  }
  memset(d, 0, sizeof *d);
  d->kind = kind;
  d->guest = obj;
  d->refs = 1;
  /* DirectInput's own default range until the game sets its own. */
  d->axis_lo = 0;
  d->axis_hi = 65535;
  if (kind == DINPUT_DEV_JOYSTICK) {
    int pad;
    x2_controller_instance_bind(&d->controller, pad_guid);
    pad = pad_of(d);
    fprintf(stderr,
            "DINPUT8: a native gamepad device at 0x%08x for pad %d "
            "(\"%s\")\n",
            obj, pad, dinput_pad_name(pad) ? dinput_pad_name(pad) : "?");
    return obj;
  }
  fprintf(stderr, "DINPUT8: a native %s device at 0x%08x%s\n", kind_name(kind),
          obj,
          dinput_system_available()
              ? " (SDL-backed)"
              : " -- with no SDL video subsystem up, so it will "
                "report nothing pressed");
  return obj;
}

void dinput_device_report(void) {
  static int done; /* see dinput_pad_report */
  int i;
  if (done++)
    return;
  if (!dinput_device_registry_count()) {
    printf("  dinput devices: none was ever created.\n");
    return;
  }
  printf("  dinput devices:\n");
  for (i = 0; i < (int)dinput_device_registry_count(); i++) {
    Device *device = dinput_device_registry_at((size_t)i);
    printf("        %-9s %u byte state, %s, %lu state read(s), %lu Poll(s),"
           " %lu Acquire(s)%s\n",
           kind_name(device->kind), device->data_size,
           device->acquired ? "acquired" : "NOT acquired", device->polls,
           device->n_poll, device->n_acquire,
           (!device->polls && !device->n_poll && !device->n_acquire)
               ? "  -- the game never touched this device after creating it"
               : "");
  }
  if (dinput_system_blind_reads())
    printf("        %lu of those read a device with no SDL video "
           "subsystem up, and reported nothing pressed.\n",
           dinput_system_blind_reads());
}
