/*
 * Native overrides of recompiled guest functions.
 *
 * Declared in src/native/overrides.json, wired by tools/gen_overrides.py. Each
 * one is a `__wrap_<symbol>`; the original body stays linked as
 * `__real_<symbol>`, so an override can defer to it and the two stay diffable
 * rather than one being deleted.
 *
 * WHY OVERRIDE RATHER THAN SATISFY. The guest asks questions about a Windows
 * machine that this host is not and is not pretending to be. Some of those
 * questions have honest answers (there is no COM registry, so CoCreateInstance
 * fails -- see ole32.c). A few gate a subsystem this port replaces outright, and
 * for those the honest move is to replace the ASKING, not to fake an answer:
 * faking one means the guest proceeds to use a thing that does not exist.
 *
 * Every override announces itself once. A run in which the game skipped a
 * check must not be indistinguishable from one in which it passed.
 */
#include "x86rt.h"
#include "x86rt_native.h"
#include "win32_sdl.h"
#include "pe_map.h"

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
 * void __cdecl FUN_00617480(void): the caller pops nothing and the original
 * ends in a plain RET, so this leaves ESP exactly as it found it and pops only
 * the return address, exactly as the recompiled body's RET would have.
 */
void __real_fn_XMen2_00617480(CPU *C);

void __wrap_fn_XMen2_00617480(CPU *C)
{
    static int said;
    if (!said++) {
        printf("override: XMen2.exe 0x00617480, the DirectX 9.0c presence "
               "check, is REPLACED.\n"
               "  It was not passed -- it was retired. This port renders "
               "natively and does not load Microsoft's D3D,\n"
               "  so the question the check asks no longer decides anything. "
               "See src/native/overrides.json.\n");
        fflush(stdout);
    }
    /* The original returns void; the caller only continues past it. Pop the
       return address the call site pushed, as the body's RET would. */
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

void __real_fn_XMen2_0055b610(CPU *C);

void __wrap_fn_XMen2_0055b610(CPU *C)
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
    __real_fn_XMen2_0055b610(C);
}


/* ---------------------------------------------------------------------
 * libIGCore 0x10069c70 -- Gap::Core::igWin32ReportBox::doModal (issue #47).
 *
 * The engine's error dialog. igReportHandler::defaultReportHandler formats
 * "<SEVERITY>:\n<message>" and calls this; the original builds a DLGTEMPLATE by
 * hand -- title "Alchemy Report Handler", buttons "&Exit", "&Debug", "&Ignore"
 * and a "Do&n't tell me again" checkbox -- and runs it through
 * DialogBoxIndirectParamA, measuring the text with DrawTextA to size it.
 *
 * Reproducing that means a dialog manager and a text renderer for a template
 * nothing else in the port uses, so the DIALOG is replaced and the MESSAGE is
 * not: same title, same buttons, same return codes.
 *
 * The codes are read from the original's own dialog procedure
 * (igWin32ReportBox::ReportDlgProc, 0x10069a30) and from what the caller does
 * with them at 0x1004c1fb, not guessed:
 *
 *     3  Exit    the caller calls exit(-1)
 *     4  Debug   the caller returns 1
 *     5  Ignore  the caller returns 0 -- the run continues
 *     6  Ignore, and do not report again   the caller returns 2
 *
 * 5 and 6 are one button plus a checkbox in the original: WM_COMMAND for the
 * Ignore button reads the checkbox with BM_GETCHECK and calls EndDialog with
 * 5 + checked. SDL's modal has no checkbox, so it becomes a fourth button --
 * the same two outcomes, offered directly.
 *
 * Ignore is the fallback for a run with no screen, because it is the only
 * answer that neither kills the process nor invents a debugger.
 */
void __real_fn_libIGCore_10069c70(CPU *C);

void __wrap_fn_libIGCore_10069c70(CPU *C)
{
    static const char *const labels[] = {
        "Exit", "Debug", "Ignore", "Ignore, don't tell me again"
    };
    static const int ids[] = { 3, 4, 5, 6 };
    const char *text = (const char *)(uintptr_t)RD32(C->esp + 4u);
    static int said;
    int answer;

    if (!said++)
        fprintf(stderr, "override: the Alchemy report box is drawn by SDL, not "
                        "by USER32's dialog manager (issue #47).\n"
                        "  Same title, same buttons, same return codes; the "
                        "checkbox is a fourth button. See src/native/"
                        "overrides.json.\n");
    answer = win32_sdl_dialog("Alchemy Report Handler",
                              text ? text : "(the report box was given no text)",
                              labels, ids, 4, 5);
    C->eax = (uint32_t)answer;
    C->esp += 8u;                /* RET 0x4: the return address and one arg */
}

/*
 * PROOF THAT IT FIRES -- x2native --dialog-selftest, wired into ctest.
 *
 * Everything above is on a path that only runs when something has already gone
 * wrong, which is the worst place for code nobody has executed. This drives the
 * override directly with a synthetic call frame and checks the three things
 * that would be silently wrong: the ABI (a __thiscall with one stack argument
 * pops eight bytes), the answer a screenless run gives, and that the message
 * reaches the log.
 *
 * The frame and the text live in a page mapped LOW, not in this file's own
 * statics: a guest pointer is 32 bits, and x2native is position-independent, so
 * the address of a static here does not fit in one. That is not a detail of the
 * test -- it is the same constraint every guest pointer in this host is under,
 * and taking the address of a host local was how the first version of this
 * crashed.
 */
#define SELFTEST_PAGE 0x00100000u

int report_box_selftest(void)
{
    static const char text[] = "SELFTEST: a report the engine would have shown";
    uint32_t esp = SELFTEST_PAGE + 0x800u;
    uint32_t textp = SELFTEST_PAGE + 0x100u;
    CPU C;
    int fails = 0;

    if (pe_map_anon_low(SELFTEST_PAGE, 0x1000u) != 0) {
        printf("x2native --dialog-selftest: could not map a page at 0x%08x for "
               "the guest frame -- NOTHING was checked.\n", SELFTEST_PAGE);
        return 1;
    }
    memcpy((void *)(uintptr_t)textp, text, sizeof text);
    memset(&C, 0, sizeof C);
    WR32(esp, 0xDEADBEEFu);                      /* the return address */
    WR32(esp + 4u, textp);                       /* the one argument */
    C.esp = esp;

    printf("x2native --dialog-selftest: the Alchemy report box, with no "
           "screen to show it on.\n");
    __wrap_fn_libIGCore_10069c70(&C);

    if (C.eax != 5u) {
        printf("  FAIL: answered %u; a run with no screen must answer 5 "
               "(Ignore), the only code that neither exits nor asks for a "
               "debugger\n", C.eax);
        fails++;
    }
    if (C.esp != esp + 8u) {
        printf("  FAIL: esp moved by %d, not 8. The original ends in RET 0x4, "
               "so it pops its return address AND its argument; anything else "
               "shifts the guest stack by a word and the damage appears "
               "somewhere else entirely\n",
               (int)(C.esp - esp));
        fails++;
    }
    printf("x2native --dialog-selftest: %s (%d failure(s)). The message text "
           "is on stderr above -- if it is not, the box swallowed it.\n",
           fails ? "FAILED" : "PASSED", fails);
    return fails;
}
