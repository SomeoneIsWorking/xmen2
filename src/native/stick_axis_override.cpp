/*
 * The gameplay sticks, analog in every direction.
 *
 * Each frame CPlayerController (0x00554840) clears its record and calls
 * 0x0061a810, which fills it by calling 0x0061a4c0 once per (slot, action):
 *
 *   cdecl float resolve(uint32_t *held, float *slots, int slot, int action,
 *                       const int *player, float sign)
 *
 * It reads the action's bound value (0x00629630: the largest magnitude over
 * the action's four bindings -- a stick axis as its positive half, 0..1) and,
 * when |value| > 0.75 (the float at 0x00682c10), ORs 1 << slot into *held and
 * stores |value|, times `sign` when `sign` is non-zero, into slots[slot]. It
 * returns slots[slot] on the x87 stack either way.
 *
 * Eight of its calls carry a sign: the movement and camera axes, Forward and
 * Backward into slot 1 (+1/-1), MoveLeft and MoveRight into slot 0 (-1/+1),
 * and the camera's four into slots 3 and 2. The rest are buttons and triggers.
 *
 * On the axes the 0.75 threshold is the whole problem. It is PER AXIS, so a
 * stick pushed anywhere but straight along one axis or hard into a corner
 * loses the component under 0.75: a diagonal at full deflection is 0.71 on
 * each axis and does not move at all, and a gentle push does nothing either.
 * The game can only be steered in the eight directions a key pad makes, which
 * is exactly how the touch stick felt. Past the gate the value is kept, not
 * normalised, and the character's movement already treats (slot 0, slot 1) as
 * a vector: measured with this override in place, it walks from a length of
 * about 0.3 in any direction (a 0.40 diagonal, 0.28 per axis, moves; 0.28
 * along one axis does not) and its speed grows with the length -- in 0.6 s a
 * 0.40 push carried the camera about 60 px, 0.67 about 106 px, 1.0 about
 * 120-160 px. Only this gate withheld all of that.
 *
 * So the axis calls here store any non-zero value, and the stick's dead zone
 * moves to where both components are known: the pad sample
 * (pad_stick_dead_zone.h). The button and trigger calls keep the retail body
 * untouched -- 0.75 is their press point, and a trigger that fired at the
 * first millimetre of travel would cast a power by accident.
 */
#include "stick_axis_override.h"

#include "guest_body.h"
#include "x2_log.h"
#include "x86rt_native.h"

#include <math.h>
#include <stdint.h>

#define EXE_PREFERRED 0x00400000u
#define RESOLVE_ACTION 0x0061a4c0u
#define FN_PADS 0x00551ed0u         /* the controller manager, a getter */
#define PADS_PLAYER_BINDINGS 0x3cu  /* vtable slot: player -> binding row */
#define FN_ACTION_VALUE 0x00629630u /* thiscall(bindings, action) -> ST(0) */
#define G_BINDING_SETS 0x00a68f40u  /* pointer per (set, row) */
#define G_BINDING_SET 0x00a6ac04u   /* the active binding set */
#define BINDINGS_ACTIONS 0x18u      /* the action table inside a set row */

static unsigned long g_axis_calls, g_axis_below_retail, g_other_calls;

static uint32_t linked(uint32_t preferred) {
  for (X86Module *module = x86_modules(); module; module = module->next) {
    if (module->preferred == EXE_PREFERRED && module->base && *module->base) {
      return *module->base + (preferred - EXE_PREFERRED);
    }
  }
  return 0;
}

/* The action's bound value as the retail body reads it, through the same two
   guest calls, on a copy of the CPU whose x87 stack is then discarded -- the
   body itself pops what 0x00629630 pushed. */
static float action_value(CPU *C, uint32_t player_slot, uint32_t action) {
  CPU call = *C;
  x86_guest_call(&call, linked(FN_PADS));
  const uint32_t pads = call.reg[kX86pEax];
  const uint32_t lookup = RD32(RD32(pads) + PADS_PLAYER_BINDINGS);

  call = *C;
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], RD32(player_slot));
  call.reg[kX86pEcx] = pads;
  x86_guest_call_args(&call, lookup, 4u);
  const uint32_t row = call.reg[kX86pEax] + RD32(linked(G_BINDING_SET)) * 4u;
  const uint32_t bindings =
      RD32(linked(G_BINDING_SETS) + row * 4u) + BINDINGS_ACTIONS;

  call = *C;
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], action);
  call.reg[kX86pEcx] = bindings;
  x86_guest_call_args(&call, linked(FN_ACTION_VALUE), 4u);
  return (float)x87_require_st0(&call, "0x00629630 returned no action value");
}

static void resolve_action(CPU *C) {
  const uint32_t esp = C->reg[kX86pEsp];
  const float sign = RDF32(esp + 24u);
  if (sign == 0.0f) {
    ++g_other_calls;
    x86_guest_body(C, "XMen2.exe", RESOLVE_ACTION);
    return;
  }
  const uint32_t held = RD32(esp + 4u);
  const uint32_t slots = RD32(esp + 8u);
  const uint32_t slot = RD32(esp + 12u);
  const float magnitude =
      fabsf(action_value(C, RD32(esp + 20u), RD32(esp + 16u)));
  const uint32_t cell = slots + slot * 4u;
  ++g_axis_calls;
  if (magnitude > 0.0f) {
    g_axis_below_retail += magnitude <= 0.75f;
    WR32(held, RD32(held) | (1u << (slot & 31u)));
    WRF32(cell, magnitude * sign);
  }
  x87_push(C, (long double)RDF32(cell));
  C->reg[kX86pEax] = slots;
  C->reg[kX86pEsp] = esp + 4u;
}

void x2_stick_axis_report(void) {
  x2_log_info("stick axes: %lu axis resolution(s) native, %lu of them below "
              "retail's 0.75 gate; %lu button/trigger resolution(s) by the "
              "retail body\n",
              g_axis_calls, g_axis_below_retail, g_other_calls);
}

__attribute__((constructor)) static void stick_axis_register_overrides(void) {
  x86_register_override("XMen2.exe", RESOLVE_ACTION, resolve_action);
}
