#include "x2_log.h"
/*
 * Native overrides that belong to BOOT and RUN CONTROL.
 *
 * These replace guest functions whose concern is how the process
 * starts and paces itself: the DirectX presence check gating engine init, the
 * frame-cap the main loop waits on, and the console command that boots into
 * the first script. Each is registered below with the module that owns its
 * entry point -- the C is the single source of truth, there is no JSON and no
 * generator. The ordinary guest body remains reachable through the JIT, so
 * the two stay diffable and an override can defer to the original by calling
 * it. Each announces itself once.
 *
 * WHY OVERRIDE RATHER THAN SATISFY. The guest asks questions about a Windows
 * machine that this host is not and is not pretending to be. Some of those
 * questions have honest answers (there is no COM registry, so CoCreateInstance
 * fails -- see ole32.c). A few gate a subsystem this port replaces outright,
 * and for those the honest move is to replace the ASKING, not to fake an
 * answer: faking one means the guest proceeds to use a thing that does not
 * exist. Every override in this directory announces itself once, so a run in
 * which the game skipped a check must not be indistinguishable from one in
 * which it passed.
 */
#include "boot_blackout.h"
#include "boot_menu_transition.h"
#include "boot_mode_runtime.h"
#include "boot_splash_policy.h"
#include "continue_runtime.h"
#include "guest_memory.h"
#include "pe_map.h"
#include "save_directory.h"
#include "settings_store.h"
#include "threads.h"
#include "win32_sdl.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lucent/cvar_c.h>

/* ---------------------------------------------------------------------
 * XMen2.exe 0x00617480 -- the DirectX 9.0c presence check (issue #18).
 *
 * The original reads Settings\DXChecked, and if that is not 1 it calls
 * FUN_00616f50, which CoCreateInstances the version-reporter COM object; a
 * false return produces the "DirectX not found" MessageBox and the game quits.
 *
 * Both ways of satisfying it are worse than replacing it. Setting DXChecked=1
 * makes the game cache a check that never ran and walk straight into
 * LoadLibraryA("d3d9.dll"), which this host correctly refuses. Returning a
 * fabricated S_OK hands the game an interface pointer to a vtable that does not
 * exist. The check is asking whether Microsoft's D3D is installed, and this
 * port's answer is that it does not use it -- so the question is retired.
 *
 * IT RETURNS A BOOL IN AL. This override used to say "void __cdecl
 * FUN_00617480(void)" and leave EAX alone, and that was wrong in the way that
 * is hardest to see: the original ends
 *
 *     006175d0  XOR AL,AL          ; false
 *     006175d2  JMP 006175d6
 *     006175d4  MOV AL,0x1         ; true
 *
 * and WinMain does `CALL 0x00617480; TEST AL,AL; JNZ ...`. Leaving EAX
 * untouched handed the game whatever the previous call had left there. When
 * its low byte happened to be zero the game took the failure path, which sets
 * BOTH 0x006f3c2c and 0x006f3a2d; the display initialiser at 0x005fb270 then
 * sees 0x006f3a2d and sets the quit flag at 0x00a09f94; WinMain skips its
 * entire main loop and returns 0 -- and skips the DISPLAY_FAILED message box
 * too, because 0x006f3c2c says the DirectX check was the reason. The result
 * is a silent exit(0) before CreateDevice with no thread ever started, which
 * is issue #54 exactly, including why it was intermittent (leftover EAX) and
 * why perturbing timing appeared to fix it.
 *
 * So the answer is written explicitly. The question is retired, and a retired
 * question's answer is "proceed" -- AL = 1, exactly as the original's true
 * path writes it, low byte only.
 */
void x2_override_00617480(CPU *C) {
  static int said;
  if (!said++) {
    x2_log_info("override: XMen2.exe 0x00617480, the DirectX 9.0c presence "
                "check, is REPLACED.\n"
                "  It was not passed -- it was retired. This port renders "
                "natively and does not load Microsoft's D3D,\n"
                "  so the question the check asks no longer decides anything. "
                "Declared in src/native/startup.c.\n");
  }
  /* TRUE, in AL only -- the original's true path is `MOV AL,0x1`, which
     leaves the rest of EAX alone, and a caller that reads EAX rather than AL
     must see what the original would have left. */
  C->reg[kX86pEax] = (C->reg[kX86pEax] & ~0xFFu) | 1u;
  /* Pop the return address the call site pushed, as the body's RET would. */
  C->reg[kX86pEsp] += 4u;
}

/* ---------------------------------------------------------------------
 * X2_UNPACED -- run the frame loop as fast as it will go.
 *
 * The game paces itself: XMen2.exe's frame function stores a minimum frame
 * time (1/30 or 1/60, from a config query) into its app object at +0x18 at its
 * own top, and then busy-waits at 0x00401ff0 until that much has elapsed. That
 * is correct behaviour and it is what a player wants -- and it is exactly
 * wrong for a test, which spends twenty-five wall seconds to see twenty-five
 * seconds of game.
 *
 * So this zeroes the cap. With it at 0 the limiter's comparison is satisfied
 * on the first read and nothing else changes: the clock still advances at real
 * speed, so animation, physics and timers all see the time they actually took.
 * A frame-rate CAP is being removed, not time being scaled -- scaling the
 * clock would make a test that "passes at 10x" say nothing about the game.
 *
 * WHY HERE. The write has to land between the store at the top of the frame
 * and the limiter, and the only guest code that runs in that window and is
 * overridable is the limiter's own first instruction: CALL 0x0055b610, the
 * timer-singleton accessor. Hooking Present instead was tried and does
 * nothing, because Present happens LATER in the frame than the limiter, so the
 * value is overwritten before it is read -- the run stayed at exactly 60fps
 * and the message claiming otherwise was printing the whole time.
 *
 * The app object is a STATIC in the exe image (0x006f3ac4), resolved through
 * the module's mapped base rather than assumed, because the exe does not have
 * to land at its preferred address.
 */
#define APP_OBJECT_RVA 0x002f3ac4u /* 0x006f3ac4 - 0x00400000 */
#define APP_FRAME_CAP 0x18u        /* float, minimum seconds/frame */

void x2_override_0055b610(CPU *C) {
  static int mode = -1; /* -1 unknown, 0 off, 1 on */
  static uint32_t field;
  static uint32_t s_guard_addr;
  static uint32_t s_inst_addr;

  if (__builtin_expect(mode < 0, 0)) {
    mode = lucent_cvar_flag("unpaced", 0) != 0;
    X86Module *m;
    for (m = x86_modules(); m; m = m->next)
      if (m->preferred == 0x00400000u && *m->base)
        break;
    if (m) {
      s_guard_addr = *m->base + (0x007ac288u - 0x00400000u);
      s_inst_addr = *m->base + (0x007ac248u - 0x00400000u);
      if (mode) {
        field = *m->base + APP_OBJECT_RVA + APP_FRAME_CAP;
        x2_log_info("X2_UNPACED: the game's frame cap at 0x%08x is zeroed "
                    "before every clock read, so the frame loop runs as "
                    "fast as it can. The clock is NOT scaled -- everything "
                    "still sees real elapsed time.\n",
                    field);
      }
    } else if (mode) {
      x2_log_error("X2_UNPACED: the exe is not mapped, so the "
                   "frame cap could not be found. The run is "
                   "PACED, whatever the variable says.\n");
      mode = 0;
    }
  }
  if (mode)
    WRF32(field, 0.0f);

  /* Fast path: once initialized, 0x0055b610 is a pure Meyers singleton getter
     returning the address of the global timer instance at 0x007ac248.
     Bypassing x86_guest_body avoids 2.8M guest SEH frame setups per 2000
     frames. */
  if (__builtin_expect(s_guard_addr && (RD8(s_guard_addr) & 1u), 1)) {
    C->reg[kX86pEax] = s_inst_addr;
    C->reg[kX86pEsp] += 4u;
    return;
  }

  x86_guest_body(C, "XMen2.exe", 0x0055b610u);
}

/* ---------------------------------------------------------------------
 * X2_BOOT_MAP -- boot straight into a level with a real new-game party.
 *
 * The exe's boot sequence (FUN_00402ba0, the "launchMap" handler the engine
 * fires on the INIT event) hardcodes the boot map name to "main" and -- since
 * the name always compares equal -- runs `runscript menus/intro_normal`:
 * six movies, then mainMenuExit. The loadMap branch the same code writes for
 * a non-"main" name is dead in the shipped binary. For a test run that
 * preamble is the cost: ~2600 frames of movies before the menu, and the
 * movies take a wall-clock time that varies run to run.
 *
 * X2_BOOT_MAP=<map> skips the movies and menus, but it MUST NOT skip new-game
 * initialization. The old implementation did exactly that: it replaced the
 * intro script with `loadmap <map> 0 0`, which asks the loader to KEEP a team
 * immediately after resetgame, when no team exists.
 *
 * The retail owner of new-game initialization is the registered BehavEd
 * command `startFirstMission` (FUN_004a7b10). It resets the gameplay managers,
 * installs the default Magneto/Cyclops/Wolverine/Storm party through the party
 * manager's own +0xf0 method, and only then runs menus/new_game (or the hard
 * variant). `startFirstMission` is a registered BehavEd command, not a console
 * command, so the host calls that exact retail function directly.
 * When it synchronously asks to run the new-game script, this override replaces
 * THAT script with `loadmap <requested map> 0 0`. Thus the only omitted work is
 * presentation: intro movies, main menu, difficulty dialog and cine01. The
 * gameplay state is initialized by the same function a normal New Game uses.
 *
 * It is deliberately NOT the default: unset (or "0") makes this a pure
 * pass-through and the boot is untouched.
 *
 * Issue #83 and C218 measured the consequence of the old ordering: five zero
 * hero handles and a suppressed tutorial conversation. That observation is the
 * regression test for this path, not an accepted limitation.
 */
#define NEW_GAME_PFX "runscript menus/new_game"
#define BOOT_PAGE 0x00110000u
#define BOOT_CONSOLE_RVA 0x0015c410u /* console vtable +0x1c, 0x0055c410 */
#define START_MISSION_RVA                                                      \
  0x000a7b10u                   /* BehavEd startFirstMission, 0x004a7b10 */
#define BOOT_MGR_VA 0x007ac290u /* &DAT_007ac290, the console singleton */

enum BootMapPhase {
  BOOT_MAP_WAITING_FOR_INTRO,
  BOOT_MAP_INITIALIZING_PARTY,
  BOOT_MAP_LOADING,
  BOOT_MAP_LOADED
};

static void boot_console_line(const CPU *source, uint32_t exe_base,
                              uint32_t command) {
  CPU K = *source;
  K.reg[kX86pEsp] -= 4u;
  WR32(K.reg[kX86pEsp], command);
  K.reg[kX86pEcx] = BOOT_MGR_VA;
  x86_guest_call_args(&K, exe_base + BOOT_CONSOLE_RVA, 4u);
}

static uint32_t mapped_exe_base(void) {
  const X86Module *module;
  for (module = x86_modules(); module; module = module->next)
    if (module->preferred == 0x00400000u && *module->base)
      return *module->base;
  return 0;
}

static int boot_to_host_mode(CPU *C, uint32_t command, uint32_t exe_base) {
  const X2BootModeDecision *decision;
  X2BootMode requested;
  if (!command ||
      !x2_boot_mode_is_intro_command(guest_memory_const_pointer(command)))
    return 0;
  requested = x2_settings_store()->boot_mode;
  decision =
      x2_boot_mode_runtime_prepare(requested, x2_retail_save_directory());
  if (decision->effective == X2_BOOT_NORMAL)
    return 0;
  if (!exe_base) {
    x2_log_error("BOOT MODE: the executable is not mapped; preserving "
                 "the normal retail boot.\n");
    return 0;
  }
  if (decision->fell_back_to_menu) {
    if (x2_boot_mode_runtime_catalog_failed())
      x2_log_error("BOOT MODE: Continue was requested but the save "
                   "directory could not be read; opening the retail "
                   "main menu.\n");
    else
      x2_log_error("BOOT MODE: Continue was requested but no valid "
                   "save exists; opening the retail main menu.\n");
  } else if (decision->effective == X2_BOOT_CONTINUE) {
    /* Direct dispatch. The boot's intro phase has already executed its
       subsystem init and `resetgame` by the time the intro command
       fires, so the retail save chain runs from the pristine state
       without the menu map, the menu, or any interaction. The ack
       re-selection supplies what the first falsified attempt lacked
       (the payload's party writes key off CPadManager's current
       player). Anything refuses: fall back to the retail menu path
       below rather than guessing. */
    x2_log_error("BOOT MODE: skipping the introduction, splash wait "
                 "and menu; dispatching the retail save chain for "
                 "%s directly.\n",
                 x2_boot_mode_runtime_continue_leaf());
    if (x2_continue_boot_dispatch(C)) {
      C->reg[kX86pEax] = 1u;
      C->reg[kX86pEsp] += 8u;
      return 1;
    }
    x2_log_error("BOOT MODE: the retail manager refused the direct "
                 "dispatch; opening the retail main menu instead.\n");
  } else
    x2_log_error("BOOT MODE: skipping the introduction and opening "
                 "the retail main menu.\n");
  /* Menu mode and the Continue fallback call the retail forced main-menu
     handler. It executes `mainmenuexit 1`, whose command owner resets and
     loads menu/main_back; the retained CMenuMain::Show intercept then
     supplies the title-screen player selection and dispatches the pending
     Continue synchronously, before any menu interaction. */
  if (!x2_boot_menu_open(C, exe_base))
    return 0;
  C->reg[kX86pEax] = 1u;
  C->reg[kX86pEsp] += 8u;
  return 1;
}

void x2_override_0055beb0(CPU *C) {
  static int mode = -1;     /* -1 unknown, 0 off, 1 on */
  static int map_requested; /* failure preserves retail normal boot */
  static uint32_t cmd;      /* guest pointer to "loadmap <map> 0 0" */
  static uint32_t exe_base;
  static enum BootMapPhase phase = BOOT_MAP_WAITING_FOR_INTRO;
  uint32_t s = RD32(C->reg[kX86pEsp] + 4u); /* param_2: the command string */

  x2_boot_splash_trace(s);

  if (mode < 0) {
    const char *e = lucent_cvar_text("boot_map");
    /* Only the exact string "0" disables: real map names start with
       digits ("0020b"), so a first-character test would silently refuse
       them and boot retail instead. */
    mode = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
    map_requested = mode;
    if (mode) {
      char buf[128];
      int len;
      exe_base = mapped_exe_base();
      if (!exe_base) {
        x2_log_error("X2_BOOT_MAP: the exe is not mapped, so the "
                     "loadmap command could not be built. Booting "
                     "normally.\n");
        mode = 0;
      } else if (pe_map_anon_low(BOOT_PAGE, 0x1000u) != 0) {
        x2_log_error("X2_BOOT_MAP: could not map a guest page for "
                     "the loadmap command. Booting normally.\n");
        mode = 0;
      } else {
        len = snprintf(buf, sizeof buf, "loadmap %s 0 0", e);
        if (len < 0 || (size_t)len >= sizeof buf) {
          x2_log_error("X2_BOOT_MAP: the requested map name is "
                       "too long for the game's 128-byte command "
                       "buffer. Booting normally.\n");
          mode = 0;
        } else {
          memcpy(guest_memory_pointer(BOOT_PAGE), buf, (size_t)len + 1u);
          cmd = BOOT_PAGE;
          x2_log_error("X2_BOOT_MAP: the boot's intro script is "
                       "replaced by the retail startFirstMission "
                       "initializer, then a direct load of \"%s\". "
                       "Unset or 0 to boot normally.\n",
                       e);
        }
      }
    }
  }
  if (!map_requested && phase == BOOT_MAP_WAITING_FOR_INTRO) {
    if (!exe_base)
      exe_base = mapped_exe_base();
    if (boot_to_host_mode(C, s, exe_base)) {
      x2_boot_splash_arm();
      x2_boot_blackout_arm(x2_boot_mode_name(x2_settings_store()->boot_mode));
      return;
    }
  }
  if (x2_boot_splash_refuse(s)) {
    C->reg[kX86pEax] = 1u;  /* "a command ran" */
    C->reg[kX86pEsp] += 8u; /* RET 0x4: return address and one argument */
    return;
  }
  if (mode && phase == BOOT_MAP_WAITING_FOR_INTRO && cmd && exe_base && s &&
      x2_boot_mode_is_intro_command(guest_memory_const_pointer(s))) {
    /* The boot has already reset the game. Run the retail New Game owner;
       its nested menus/new_game command is intercepted below only after
       it has installed the default party. */
    CPU K = *C;
    phase = BOOT_MAP_INITIALIZING_PARTY;
    x86_guest_call_args(&K, exe_base + START_MISSION_RVA, 0u);
    if (phase != BOOT_MAP_LOADED) {
      x2_log_error("X2_BOOT_MAP: the retail startFirstMission "
                   "function returned without requesting its "
                   "new-game script. Refusing a bare map load because "
                   "it would create a level with no party; continuing "
                   "through the normal intro.\n");
      mode = 0;
      x86_guest_body(C, "XMen2.exe", 0x0055beb0u);
      return;
    }
    C->reg[kX86pEax] =
        1u; /* "a command ran"; the boot does not read EAX here */
    C->reg[kX86pEsp] += 8u; /* RET 0x4: the return address and the one arg */
    return;
  }
  if (mode && phase == BOOT_MAP_INITIALIZING_PARTY && cmd && exe_base && s &&
      strncmp(guest_memory_const_pointer(s), NEW_GAME_PFX,
              sizeof NEW_GAME_PFX - 1u) == 0) {
    /* startFirstMission has completed the real party setup and is about to
       run the presentation-only new_game script. Replace that script's
       movie and hardcoded tutorial map with the requested map. */
    phase = BOOT_MAP_LOADING;
    boot_console_line(C, exe_base, cmd);
    phase = BOOT_MAP_LOADED;
    x2_log_error("X2_BOOT_MAP: startFirstMission installed the retail "
                 "default party; loading the requested map now.\n");
    C->reg[kX86pEax] = 1u;
    C->reg[kX86pEsp] += 8u;
    return;
  }
  x86_guest_body(C, "XMen2.exe", 0x0055beb0u);
}

/* FUN_00402ba0 -- the boot frontend's intro phase. Retail holds the legal
 * splash on screen by comparing the guest clock against the phase's own
 * start timestamp ([this+0x24]) and a 5-second duration constant in .rdata;
 * only then does the tick run its subsystem init, `resetgame` and the intro
 * command. A Continue boot wants none of that wait, and neither does a Menu
 * boot -- the splash is a timed screen, not a keypress-gated one, so waiting
 * it out is dead time in every automated or impatient launch. While the
 * persisted boot mode asks for Continue OR Menu, the phase's start timestamp
 * is marked long past BEFORE the original body runs, so the phase's own
 * logic -- unchanged, its duration constant unread -- passes the wait on the
 * first tick and the intercepted intro command dispatches the mode's path.
 * Every effect of the phase except the delay is the retail effect. Normal
 * boot keeps the retail wait. */

void x2_override_00402ba0(CPU *C) {
  static const float long_past = -1.0e9f;
  static int reported;
  uint32_t phase = C->reg[kX86pEcx];
  X2BootMode mode = x2_settings_store()->boot_mode;
  uint32_t bits;
  float was;

  /* Report on the first tick whichever way it goes. An override that only
     spoke when it acted would be indistinguishable, in a log, from one that
     was never reached -- and "the splash was skipped" is exactly the claim
     a silent negative would let through. */
  if (phase && (mode == X2_BOOT_CONTINUE || mode == X2_BOOT_MENU)) {
    bits = RD32(phase + 0x24u);
    memcpy(&was, &bits, sizeof was);
    memcpy(&bits, &long_past, sizeof bits);
    WR32(phase + 0x24u, bits);
    if (!reported) {
      reported = 1;
      x2_log_error("BOOT SPLASH: intro phase start stamp %.3f -> "
                   "%.1f, so the phase's own wait is already past on "
                   "its first tick and the intro command dispatches "
                   "the %s boot path immediately.\n",
                   (double)was, (double)long_past, x2_boot_mode_name(mode));
    }
  } else if (!reported) {
    reported = 1;
    x2_log_error("BOOT SPLASH: retail splash wait left intact (boot "
                 "mode %s, phase %s).\n",
                 x2_boot_mode_name(mode), phase ? "present" : "NULL");
  }
  x86_guest_body(C, "XMen2.exe", 0x00402ba0u);
}

/* Register this file's overrides. Runs before main; the dispatcher consults
   the table only when the guest actually calls one of these entry points. */
__attribute__((constructor)) static void x2_startup_register_overrides(void) {
  x86_register_override("XMen2.exe", 0x00617480, x2_override_00617480);
  x86_register_override("XMen2.exe", 0x0055b610, x2_override_0055b610);
  x86_register_override("XMen2.exe", 0x0055beb0, x2_override_0055beb0);
  x86_register_override("XMen2.exe", 0x00402ba0, x2_override_00402ba0);
}
