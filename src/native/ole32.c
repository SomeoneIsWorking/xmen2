/*
 * ole32 -- COM, of which this game uses almost none.
 *
 * XMen2.exe imports exactly three named entry points: CoInitialize,
 * CoCreateInstance and CoUninitialize. It makes ONE CoCreateInstance call, for
 *
 *     CLSID {A65B8071-3BFE-4213-9A5B-491DA4461CA7}
 *     IID   {9C6B4CB0-23F8-49CC-A3ED-45A55000A6D2}
 *
 * which are not Windows classes -- a third-party in-process server that shipped
 * with the game or was expected on the machine.
 *
 * The important thing is what the caller does with the result. At 0x00616f8a it
 * is `call CoCreateInstance; test eax,eax; jl <skip>`: the game checks the
 * HRESULT and branches past the whole feature when it fails. So the honest
 * answer here is a failure, and specifically REGDB_E_CLASSNOTREG -- "no such
 * class is registered" -- because that is literally true of this host. There is
 * no COM registry, nothing implements that interface, and saying so is not a
 * stub standing in for an implementation.
 *
 * Returning S_OK with a fabricated interface pointer WOULD be that, and would
 * be far worse: the game would immediately call through a vtable that does not
 * exist. The difference between "not implemented" and "quietly wrong" is the
 * whole of it.
 */
#include "x86rt.h"
#include "x86rt_native.h"
#include "guest_memory.h"

#include <stdio.h>

#define A(i)  RD32(C->esp + 4u + (uint32_t)(i) * 4u)

static void ret_std(CPU *C, uint32_t eax, int nargs)
{
    C->eax = eax;
    C->esp += 4u + (uint32_t)nargs * 4u;
}

#define S_OK                 0x00000000u
#define S_FALSE              0x00000001u
#define REGDB_E_CLASSNOTREG  0x80040154u

/* Reported once, so a run that depends on COM says so rather than leaving the
   reader to infer it from a feature quietly not happening. */
static int g_told;

void imp_ole32_CoInitialize(CPU *C)
{
    /* Truthful: there is a single apartment here and it is now initialised, in
       the only sense this host has -- nothing about COM's threading model is
       being promised, and nothing asks. */
    ret_std(C, S_OK, 1);
}

void imp_ole32_CoInitializeEx(CPU *C) { ret_std(C, S_OK, 2); }
void imp_ole32_CoUninitialize(CPU *C) { ret_std(C, 0, 0); }

void imp_ole32_CoCreateInstance(CPU *C)
{
    /* (rclsid, pUnkOuter, dwClsContext, riid, ppv) */
    uint32_t rclsid = A(0), ppv = A(4);
    if (!g_told) {
        g_told = 1;
        if (rclsid) {
            const unsigned char *g = guest_memory_const_pointer(rclsid);
            fprintf(stderr,
                "ole32: CoCreateInstance for CLSID "
                "{%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X}\n"
                "  There is no COM registry here and nothing implements it, so "
                "this returns REGDB_E_CLASSNOTREG -- which is true.\n"
                "  The caller checks the HRESULT and skips the feature; "
                "returning S_OK with an invented interface pointer would make "
                "it call through a vtable that does not exist.\n",
                g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6],
                g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
        }
    }
    /* COM requires *ppv be NULLed on failure, and a caller that checks the
       pointer instead of the HRESULT depends on it. */
    if (ppv) WR32(ppv, 0);
    ret_std(C, REGDB_E_CLASSNOTREG, 5);
}
