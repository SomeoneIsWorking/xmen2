/*
 * Native overrides that belong to BOOT and RUN CONTROL.
 *
 * These replace recompiled guest functions whose concern is how the process
 * starts and paces itself: the DirectX presence check gating engine init, the
 * frame-cap the main loop waits on, and the console command that boots into
 * the first script. Each is registered below with the module that owns its
 * entry point -- the C is the single source of truth, there is no JSON and no
 * generator. The recompiled body stays emitted and linked under its own
 * fn_<module>_<ep> name, so the two stay diffable and an override can defer to
 * the original by calling it. Each announces itself once.
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
#include "x86rt.h"
#include "x86rt_native.h"
#include "win32_sdl.h"
#include "pe_map.h"
#include "threads.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
void x2_override_00617480(CPU *C)
{
    static int said;
    if (!said++) {
        printf("override: XMen2.exe 0x00617480, the DirectX 9.0c presence "
               "check, is REPLACED.\n"
               "  It was not passed -- it was retired. This port renders "
               "natively and does not load Microsoft's D3D,\n"
               "  so the question the check asks no longer decides anything. "
               "Declared in src/native/startup.c.\n");
        fflush(stdout);
    }
    /* TRUE, in AL only -- the original's true path is `MOV AL,0x1`, which
       leaves the rest of EAX alone, and a caller that reads EAX rather than AL
       must see what the original would have left. */
    C->eax = (C->eax & ~0xFFu) | 1u;
    /* Pop the return address the call site pushed, as the body's RET would. */
    C->esp += 4u;
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
#define APP_OBJECT_RVA   0x002f3ac4u          /* 0x006f3ac4 - 0x00400000 */
#define APP_FRAME_CAP    0x18u                /* float, minimum seconds/frame */

void fn_XMen2_0055b610(CPU *C);

void x2_override_0055b610(CPU *C)
{
    static int mode = -1;                     /* -1 unknown, 0 off, 1 on */
    static uint32_t field;

    if (mode < 0) {
        const char *e = getenv("X2_UNPACED");
        mode = (e && *e && *e != '0') ? 1 : 0;
        if (mode) {
            X86Module *m;
            for (m = x86_modules(); m; m = m->next)
                if (m->preferred == 0x00400000u && *m->base) break;
            if (!m) {
                fprintf(stderr, "X2_UNPACED: the exe is not mapped, so the "
                                "frame cap could not be found. The run is "
                                "PACED, whatever the variable says.\n");
                mode = 0;
            } else {
                field = *m->base + APP_OBJECT_RVA + APP_FRAME_CAP;
                printf("X2_UNPACED: the game's frame cap at 0x%08x is zeroed "
                       "before every clock read, so the frame loop runs as "
                       "fast as it can. The clock is NOT scaled -- everything "
                       "still sees real elapsed time.\n", field);
                fflush(stdout);
            }
        }
    }
    if (mode) WRF32(field, 0.0f);
    fn_XMen2_0055b610(C);
}


/* ---------------------------------------------------------------------
 * X2_BOOT_MAP -- boot straight into a level, for testing only.
 *
 * The exe's boot sequence (FUN_00402ba0, the "launchMap" handler the engine
 * fires on the INIT event) hardcodes the boot map name to "main" and -- since
 * the name always compares equal -- runs `runscript menus/intro_normal`:
 * six movies, then mainMenuExit. The loadMap branch the same code writes for
 * a non-"main" name is dead in the shipped binary. For a test run that
 * preamble is the cost: ~2600 frames of movies before the menu, and the
 * movies take a wall-clock time that varies run to run.
 *
 * X2_BOOT_MAP=<map> skips the whole preamble. When the console executor is
 * asked to run that one boot script, the override feeds the game's OWN
 * map-loading command -- `loadmap <map> 0 0` -- through the same console
 * command-line path the script command loadMapKeepTeam uses (FUN_004a0cc0
 * builds exactly that string and runs it through console +0x1c, FUN_0055c410).
 * The "0 0" is loadmap's keep-team mode, which is where the tutorial's
 * new_game.py ends. The engine is already past resetgame by the time this
 * runs, because the boot calls resetgame BEFORE the intro script.
 *
 * It is deliberately NOT the default: unset (or "0") makes this a pure
 * pass-through and the boot is untouched.
 *
 * WHAT IT COSTS, measured (C218, issue #83). The preamble it skips is where the
 * PARTY is built, so a boot-map run has no player character at all: all five
 * hero handles -- 0x0070b814[0..3] and the 0x0072988c fallback -- stay 0, where
 * a normally-booted run resolves player 0's. `tools/x2ctl.py input` reports
 * them, and that one line is the cheap check for whether a run is comparable to
 * a played game.
 *
 * That is not cosmetic. Anything downstream of the player actor behaves
 * differently: the tutorial's second conversation is SUPPRESSED in a boot-map
 * run, because igConversationManager::start cannot resolve a speaker, falls
 * back to a call site that bases the seen-line bitmap at 0, and collides with
 * the first conversation -- so the script that undoes `lockControls(-1)` never
 * runs and the level looks soft-locked. Three sessions read that as a port
 * defect before the two boot paths were compared. It is this shortcut's own
 * limitation.
 */
#define BOOT_SCRIPT_PFX   "runscript menus/intro_normal"
#define BOOT_PAGE         0x00110000u
#define BOOT_CONSOLE_RVA  0x0015c410u   /* console vtable +0x1c, 0x0055c410 */
#define BOOT_MGR_VA       0x007ac290u   /* &DAT_007ac290, the console singleton */

void fn_XMen2_0055beb0(CPU *C);

void x2_override_0055beb0(CPU *C)
{
    static int mode = -1;               /* -1 unknown, 0 off, 1 on */
    static uint32_t cmd;                /* guest pointer to "loadmap <map> 0 0" */
    static uint32_t exe_base;
    uint32_t s = RD32(C->esp + 4u);     /* param_2: the command string */

    if (mode < 0) {
        const char *e = getenv("X2_BOOT_MAP");
        mode = (e && *e && *e != '0') ? 1 : 0;
        if (mode) {
            const X86Module *m;
            char buf[128];
            exe_base = 0;
            for (m = x86_modules(); m; m = m->next)
                if (m->preferred == 0x00400000u && *m->base) {
                    exe_base = *m->base;
                    break;
                }
            if (!exe_base) {
                fprintf(stderr, "X2_BOOT_MAP: the exe is not mapped, so the "
                                "loadmap command could not be built. Booting "
                                "normally.\n");
                mode = 0;
            } else if (pe_map_anon_low(BOOT_PAGE, 0x1000u) != 0) {
                fprintf(stderr, "X2_BOOT_MAP: could not map a guest page for "
                                "the loadmap command. Booting normally.\n");
                mode = 0;
            } else {
                snprintf(buf, sizeof buf, "loadmap %s 0 0", e);
                memcpy((void *)(uintptr_t)BOOT_PAGE, buf, strlen(buf) + 1u);
                cmd = BOOT_PAGE;
                fprintf(stderr, "X2_BOOT_MAP: the boot's intro script is "
                                "replaced by a direct map load of \"%s\" "
                                "(console: loadmap %s 0 0). Unset or 0 to boot "
                                "normally.\n", e, e);
                fflush(stderr);
            }
        }
    }
    if (mode && cmd && exe_base && s &&
        strncmp((const char *)(uintptr_t)s, BOOT_SCRIPT_PFX,
                sizeof BOOT_SCRIPT_PFX - 1u) == 0) {
        /* The boot asked to run the intro. Load the map instead, through the
           console's own command-line path, exactly as loadMapKeepTeam does. */
        CPU K = *C;
        K.esp -= 4u;
        WR32(K.esp, cmd);
        K.ecx = BOOT_MGR_VA;
        x86_guest_call_args(&K, exe_base + BOOT_CONSOLE_RVA, 4u);
        C->eax = 1u;      /* "a command ran"; the boot does not read EAX here */
        C->esp += 8u;     /* RET 0x4: the return address and the one arg */
        return;
    }
    fn_XMen2_0055beb0(C);
}

/* Register this file's overrides. Runs before main; the dispatcher consults
   the table only when the guest actually calls one of these entry points. */
__attribute__((constructor))
static void x2_startup_register_overrides(void)
{
    x86_register_override("XMen2.exe", 0x00617480, x2_override_00617480);
    x86_register_override("XMen2.exe", 0x0055b610, x2_override_0055b610);
    x86_register_override("XMen2.exe", 0x0055beb0, x2_override_0055beb0);
}