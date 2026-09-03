/* Hotswap and controller-table invariant ownership. See dinput8_hotplug.h. */
#include "dinput8_hotplug.h"

#include "controller_hotplug.h"
#include "dinput8_controller_slots.h"
#include "dinput_pad.h"
#include "player_input.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

/* The game's enumeration identity, remembered from its first GAMECTRL
   EnumDevices; the pump re-enters the guest through it. */
static uint32_t g_pad_cb, g_pad_ref, g_pad_enum;
static const char *g_pad_enum_name;
static X2ControllerHotplug g_hotplug;

void dinput8_check_controller_table(void);

/*
 * HOTSWAP: synchronize the game when the controller inventory changes.
 *
 * Called once a frame from the first input call of the frame (the keyboard's
 * GetDeviceState -- XMen2.exe's per-frame update FUN_006285c0 reads it at
 * 0x0062861e before anything else), so the guest is between operations rather
 * than in the middle of its own device loop.
 *
 * This RE-ENTERS the guest, which is the same thing the enumeration itself
 * does, and is why the pump point matters: the callback creates a device,
 * sets its data format and enumerates its axes, all of which come back through
 * this host. Doing it from inside the joystick loop would be inserting a
 * device into a table the game is walking.
 *
 * Each inventory generation is admitted once. A GUID cache cannot express
 * disconnect (which also has to clear the guest's attached flags), and a
 * process-lifetime cache bounded by the simultaneous eight-pad limit starts
 * re-enumerating every frame after eight reconnects.
 */
void dinput8_hotplug_pump(struct CPU *cpu) {
  CPU *C = cpu;
  uint64_t generation;
  if (!C)
    return;
  dinput_pad_refresh();
  x2_player_input_sync(C);
  dinput8_check_controller_table();
  generation = dinput_pad_generation();
  if (!x2_controller_hotplug_needs_admission(&g_hotplug, generation))
    return;
  if (!g_pad_ref) {
    static int told_ref;
    if (!told_ref++)
      fprintf(stderr, "DINPUT8: a re-admission is pending but the "
                      "game never enumerated controllers, so there is "
                      "no routine to call.\n");
    return;
  }

  if (!g_pad_enum) {
    static int told;
    if (!told++)
      fprintf(stderr,
              "DINPUT8: a pad appeared, and this host never "
              "identified the game's enumeration routine, so "
              "generation %llu cannot be synchronized.\n",
              (unsigned long long)generation);
    return;
  }
  fprintf(stderr,
          "DINPUT8: HOTSWAP -- controller inventory generation %llu "
          "now has %d connected pad(s). Calling the game's own "
          "enumeration routine at 0x%08x so disconnects and arrivals "
          "are applied by the game's rules.\n",
          (unsigned long long)generation, dinput_pad_count(), g_pad_enum);
  x2_controller_hotplug_admitted(&g_hotplug);
  {
    /* __thiscall FUN_00628e20(BOOL bRecordNew): ECX = the input manager,
       one stack argument. TRUE is what admits a controller the game has
       not seen before -- the same value startup passes. */
    CPU K = *C;
    K.ecx = g_pad_ref;
    K.esp -= 4u;
    WR32(K.esp, 1u);
    x86_guest_call_args(&K, g_pad_enum, 4u);
  }
}

/*
 * The game's controller table must reflect the LIVE inventory.
 *
 * A save payload deserializes the input manager's controller table with the
 * identities the SAVING machine's devices had. This host synthesizes live
 * instance identities per session, so a restored table can name devices that
 * will never exist again while a connected pad sits recorded nowhere and is
 * never polled -- measured: after a Continue load the game read a pad button
 * 0 more times for the rest of the run. Retail does not need this check
 * because Windows DirectInput instance GUIDs are persistent; maintaining the
 * invariant here re-runs the game's OWN enumeration -- the same routine
 * hotswap calls -- so the table is re-derived by the game's rules rather
 * than edited.
 */
static int table_matches_live_inventory(void) {
  int slot, live = dinput_pad_count();
  int matched = 0;

  for (slot = 0; slot < DINPUT8_CONTROLLER_SLOTS; slot++)
    if (dinput8_controller_host_pad_for_slot(slot) >= 0)
      matched++;
  return live == 0 || matched > 0;
}

void dinput8_check_controller_table(void) {
  static int was_broken;
  int broken;

  if (!g_pad_enum || !g_pad_ref)
    return;
  broken = !table_matches_live_inventory();
  if (!broken) {
    was_broken = 0;
    return;
  }
  if (was_broken)
    return;
  was_broken = 1;
  fprintf(stderr,
          "DINPUT8: the game's controller table names NO live pad "
          "while %d pad(s) are connected -- a restored save's "
          "device table replaced the live inventory. Re-running "
          "the game's enumeration so it re-admits them by its own "
          "rules.\n",
          dinput_pad_count());
  x2_controller_hotplug_invalidate(&g_hotplug);
}

void dinput8_hotplug_note_game_enumeration(unsigned int callback,
                                           unsigned int manager_ref,
                                           unsigned int routine,
                                           const char *routine_name) {
  g_pad_cb = callback;
  g_pad_ref = manager_ref;
  if (routine && routine != g_pad_enum) {
    g_pad_enum = routine;
    g_pad_enum_name = routine_name;
  }
}

void dinput8_hotplug_enumerated(unsigned long long generation, int connected,
                                int reported) {
  x2_controller_hotplug_enumerated(&g_hotplug, generation, connected, reported);
}

unsigned long dinput8_hotplug_admissions(void) { return g_hotplug.admissions; }
