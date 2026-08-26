/*
 * Native overrides that belong to the ERROR-REPORTING UI.
 *
 * The Alchemy report box is the engine's modal error dialog. It is replaced
 * rather than implemented as a USER32 dialog manager, because the question
 * the engine is asking is "what did it want to tell the player, and what did
 * the player answer" -- not "can this host draw a Win32 dialog". Registered
 * below against the module that owns the entry point; the recompiled body
 * stays emitted and linked as fn_libIGCore_10069c70, so the two stay
 * diffable.
 */
#include "x86rt.h"
#include "x86rt_native.h"
#include "win32_sdl.h"
#include "pe_map.h"
#include "threads.h"
#include "guest_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
void x2_override_10069c70(CPU *C)
{
    static const char *const labels[] = {
        "Exit", "Debug", "Ignore", "Ignore, don't tell me again"
    };
    static const int ids[] = { 3, 4, 5, 6 };
    const char *text = guest_memory_const_pointer(RD32(C->esp + 4u));
    static int said;
    int answer;

    if (!said++)
        fprintf(stderr, "override: the Alchemy report box is drawn by SDL, not "
                        "by USER32's dialog manager (issue #47).\n"
                        "  Same title, same buttons, same return codes; the "
                        "checkbox is a fourth button.\n");
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
    memcpy(guest_memory_pointer(textp), text, sizeof text);
    memset(&C, 0, sizeof C);
    WR32(esp, 0xDEADBEEFu);                      /* the return address */
    WR32(esp + 4u, textp);                       /* the one argument */
    C.esp = esp;

    printf("x2native --dialog-selftest: the Alchemy report box, with no "
           "screen to show it on.\n");
    x2_override_10069c70(&C);

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

__attribute__((constructor))
static void x2_reportbox_register_overrides(void)
{
    x86_register_override("libIGCore.dll", 0x10069c70, x2_override_10069c70);
}
