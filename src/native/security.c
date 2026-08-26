/*
 * Native overrides for the /GS stack-cookie mechanism (MSVC buffer-overrun
 * protection), which is where an intermittent load crash surfaces.
 *
 * The recompiled exe ends ~1001 /GS functions with a direct call to
 * __security_check_cookie (0x00672161): the epilogue loads its stored local
 * cookie into ECX and calls it; it compares ECX against the process cookie at
 * 0x006f38f8 and, on a mismatch, tail-jumps to __report_gsfailure (0x00672130)
 * which raises the CRT security error. A mismatch means SOME function's local
 * frame was overrun (a stack buffer written past its end) -- the corruption is
 * the bug, and this check is the tripwire that names it.
 *
 * The check is a DIAGNOSTIC override: it reproduces the original's exact
 * behavior (compare, return on match, defer to __report_gsfailure on
 * mismatch) but, on mismatch, first names the CALLER -- the guest return
 * address on the stack is the /GS function that just failed its own epilogue
 * check. That is the function to rewrite. It defers to the retained
 * __report_gsfailure body for the actual abort, so nothing about the failure
 * path changes.
 */

/* The process-wide cookie, as the recompiled check reads it. */
#define X2_COOKIE_VA     0x006f38f8u

#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>

void fn_XMen2_00672130(CPU *C);   /* __report_gsfailure: raises the CRT abort */

void x2_override_00672161(CPU *C)
{
    uint32_t cookie = RD32(X2_COOKIE_VA);
    if (C->ecx == cookie) {
        /* The frame is intact; return as the body's RET does. */
        C->esp += 4u;
        return;
    }
    {
        /* The caller's return address names the /GS function that just failed
           its own epilogue check. The cookie compare reads ECX, which the
           caller loaded from its stored local -- so a mismatch means that
           caller's frame was overrun. */
        uint32_t ra = RD32(C->esp);
        const char *nm = x86_native_name_at(ra);
        X86Module *m = x86_module_for(ra);
        fprintf(stderr,
                "SECURITY: __security_check_cookie MISMATCH in the /GS epilogue "
                "of caller 0x%08x%s%s%s (stored cookie 0x%08x != process "
                "0x%08x). A function wrote past its own stack frame.\n",
                ra, nm ? " " : " (",
                nm ? nm : (m ? m->name : "???"),
                nm ? "" : (m ? " +offset)" : ")"),
                C->ecx, cookie);
        /* For FUN_0046b750 (caller 0x0046badd) the cookie sits at entry_esp-4;
           report the current guest stack so the exact slot can be computed for
           a follow-up memory watch. */
        fprintf(stderr, "  guest esp=0x%08x ebp=0x%08x\n", C->esp, C->ebp);
        /* Reconstruct the guest call chain by walking return addresses on the
           stack (each looks like a code address in a mapped module). This
           names every function alive at the moment the cookie was checked --
           the writer is on this chain. */
        {
            /* Walk DOWN the guest stack (deeper frames are at lower guest
               addresses) collecting words that look like mapped code. The
               cookie check's ESP is near the top of the live frame; the
               return addresses that name the call chain are below it. */
            uint32_t sp = C->esp;
            int depth;
            for (depth = 0; depth < 64 && sp > 0x700f0000u; sp -= 4) {
                uint32_t w = RD32(sp);
                const char *nm = x86_native_name_at(w);
                X86Module *mm = x86_module_for(w);
                if (nm || mm) {
                    fprintf(stderr, "  [sp 0x%08x] 0x%08x %s%s%s\n",
                            sp, w, nm ? "" : "in ",
                            nm ? nm : (mm ? mm->name : "???"),
                            (nm || !mm) ? "" : " +offset");
                    depth++;
                }
            }
        }
        /* Dump the guest stack words around the cookie slot: the cookie sits
           at the top of the frame, so what surrounds it says what kind of
           write zeroed it (a local buffer run-on vs a callee clobber). */
        {
            uint32_t a;
            for (a = 0x700ffd80; a <= 0x700fff40; a += 16) {
                fprintf(stderr, "  [0x%08x] %08x %08x %08x %08x\n",
                        a, RD32(a), RD32(a + 4), RD32(a + 8), RD32(a + 0xc));
            }
        }
    }
    /* Defer to __report_gsfailure, which raises the CRT security abort. It is
       a tail call in the original; keep the stack as-is and let it run. */
    fn_XMen2_00672130(C);
}

/* X2_SECURITY_WATCH=1: arm the WRITE watch on FUN_0046b750's cookie slot the
   moment it enters, so the /GS overrun's WRITER is caught by address instead
   of by the dispatch ring (which a DIRECT-call writer never touches). The
   cookie sits at entry_esp - 4 (see the epilogue: [ESP+0x20] after SUB 0x1c +
   PUSH ESI + PUSH EDI). Disarmed after the body returns in a good run. */
extern volatile uint32_t x2_write_watch_addr;
static int g_security_watch_armed;

void fn_XMen2_0046b750(CPU *C);

void x2_override_0046b750_watch(CPU *C)
{
    static int want = -1;
    if (want < 0) {
        const char *e = getenv("X2_SECURITY_WATCH");
        want = (e && *e && *e != '0') ? 1 : 0;
    }
    if (want) {
        uint32_t cookie_addr = C->esp - 4u;   /* the /GS cookie slot */
        uint32_t entry_esp = C->esp;
        x2_write_watch_addr = cookie_addr;
        g_security_watch_armed = 1;
        fprintf(stderr, "SECURITY: FUN_0046b750 entry esp 0x%08x, cookie slot "
                        "0x%08x.\n", entry_esp, cookie_addr);
        x86_stackcheck_arm(1);
        fn_XMen2_0046b750(C);
        x86_stackcheck_arm(0);
        /* The epilogue reads [ESP+0x20] against the value stored here. If the
           body did not return esp to where it started, those are different
           slots and the /GS check compares the wrong word -- which reads as a
           buffer overrun and is not one. Say so, with both numbers. */
        if (C->esp != entry_esp + 4u)
            fprintf(stderr, "SECURITY: FUN_0046b750 returned esp 0x%08x, "
                            "expected 0x%08x -- the body is %d byte(s) out of "
                            "balance, so its /GS epilogue reads a slot that "
                            "is NOT the cookie it stored.\n",
                    C->esp, entry_esp + 4u,
                    (int)(C->esp - (entry_esp + 4u)));
        g_security_watch_armed = 0;
        x2_write_watch_addr = 0;
        return;
    }
    fn_XMen2_0046b750(C);
}

__attribute__((constructor))
static void x2_security_register_overrides(void)
{
    x86_register_override("XMen2.exe", 0x00672161, x2_override_00672161);
    x86_register_override("XMen2.exe", 0x0046b750, x2_override_0046b750_watch);
}
