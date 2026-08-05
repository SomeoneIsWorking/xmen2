/*
 * x2native -- the recompiled game code running as a NATIVE Linux binary.
 *
 * No Wine, no PE loader, no original DLL alongside us. This is the first
 * artefact on the rc-native track, and its job is to make that track
 * measurable rather than aspirational: it maps the original image at its own
 * base, runs recompiled function bodies against it, and opens an SDL window so
 * the platform layer that replaces USER32/DINPUT/D3D8 has somewhere to live.
 *
 *   ./x2native <libIGDisplay.dll> [--no-window]
 *
 * What it does NOT do, and must not be read as doing: run the game. 107
 * imports are stubbed to abort by name (61 of them into libIGCore, which is
 * another module to recompile; the rest are the Win32 surface SDL replaces).
 * Any function that reaches one stops with that name printed. That is the
 * remaining work, stated as a list rather than as a guess.
 */
#include "pe_map.h"
#include "x86rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

int         x86_native_call(uint32_t ep, CPU *C);
const char *x86_native_name(uint32_t ep);
extern const int g_fn_count;

uint32_t g_fsbase, g_gsbase;

void x86_seg_unset(const char *seg)
{
    fprintf(stderr, "x86_seg_unset: guest code read %s-relative memory before "
                    "the native host set a %s base. On Windows this is the "
                    "TIB; here it has to be modelled, and it has not been "
                    "yet.\n", seg, seg);
    abort();
}

/* ---- the guest stack ---------------------------------------------------
 *
 * Ours, not the C stack. This is the one place the native build is simpler
 * than the Wine-hosted one: there, recompiled code is entered BY Windows code
 * and the guest stack is the real thread stack, which is what let host callees
 * run their frames over the runtime's own state (C080). Here the two are
 * different memory by construction.
 */
#define GUEST_STACK 0x00100000u
static uint32_t guest_stack_top;

static int guest_stack_init(void)
{
    /* Below 4 GB like everything else the guest addresses, and mapped rather
       than malloc'd so its address is predictable in a fault report. */
    if (pe_map_anon_low(0x30000000u, GUEST_STACK) != 0) return -1;
    guest_stack_top = 0x30000000u + GUEST_STACK - 64u;
    return 0;
}

/* ---- the battery -------------------------------------------------------
 *
 * Real postconditions, derived from the guest instructions, not "it did not
 * crash". Each case POISONS what it is going to check first, so a body that
 * did nothing at all fails instead of inheriting a value that was already
 * right -- which is the way a battery like this usually lies.
 *
 * Every case also reads or writes the MAPPED IMAGE. Leaf functions that only
 * touch registers would pass against a blank page and would say nothing about
 * the thing this binary exists to establish.
 */
#define SCRATCH 0x30200000u          /* a guest-addressable scratch object */

static int fails;
/*
 * The negative control, in the shipping binary.
 *
 * A battery whose checks all pass is indistinguishable from a battery whose
 * expectations do not bind -- poison a value, forget to run the body, and
 * "0 FAILED" reads exactly the same. --selftest skips one body ON PURPOSE: the
 * poisoned values must then be reported as failures, and if they are not, every
 * other line of output this run is worthless and it says so.
 */
static int selftest, skip_body;

static int call_body(uint32_t ep, CPU *C)
{
    if (skip_body) return 1;              /* pretend it ran; nothing changes */
    return x86_native_call(ep, C);
}

static int checks;

static void check(const char *what, uint32_t got, uint32_t want)
{
    checks++;
    if (got == want) {
        printf("    ok    %-34s 0x%08x\n", what, got);
    } else {
        printf("    FAIL  %-34s got 0x%08x, want 0x%08x\n", what, got, want);
        fails++;
    }
}

static uint32_t gr32(uint32_t a) { return *(volatile uint32_t *)(uintptr_t)a; }
static void gw32(uint32_t a, uint32_t v) { *(volatile uint32_t *)(uintptr_t)a = v; }
static uint8_t gr8(uint32_t a) { return *(volatile uint8_t *)(uintptr_t)a; }
static void gw8(uint32_t a, uint8_t v) { *(volatile uint8_t *)(uintptr_t)a = v; }

/* Set up a guest frame: return address plus `nargs` stack arguments. */
static void frame(CPU *C, const uint32_t *args, int nargs)
{
    int i;
    memset(C, 0, sizeof *C);
    C->esp = guest_stack_top - (uint32_t)(nargs + 1) * 4u;
    gw32(C->esp, 0xDEADBEEFu);                    /* the return address */
    for (i = 0; i < nargs; i++) gw32(C->esp + 4u + (uint32_t)i * 4u, args[i]);
}

static void case_enumerate(void)
{
    CPU C; uint32_t args[2] = { 0, 0 }, esp0;
    /* MOV byte [0x10021d4c],1 ; XOR EAX,EAX ; RET 8 */
    printf("  0x10005660 igWin32Window::enumerateMouseAndKeyboard\n");
    gw8(g_imgbase + 0x21d4cu, 0xA5);              /* poison */
    frame(&C, args, 2);
    esp0 = C.esp;
    C.eax = 0x11223344u;                          /* must be zeroed by XOR */
    if (!call_body(0x10005660u, &C)) { printf("    FAIL  not in table\n"); fails++; return; }
    check("byte [imgbase+0x21d4c]", gr8(g_imgbase + 0x21d4cu), 1u);
    check("eax after XOR EAX,EAX", C.eax, 0u);
    check("esp delta (RET 8 = 4+8)", C.esp - esp0, 12u);
}

static void case_getscreensize(void)
{
    CPU C; uint32_t args[2] = { SCRATCH, SCRATCH + 16u }, esp0;
    /* [arg0] = [0x10021d54] ; [arg1] = [0x10021d58] ; RET 8 */
    printf("  0x10006280 igWin32Window::getScreenSize\n");
    gw32(g_imgbase + 0x21d54u, 0x00000320u);      /* poison the globals with */
    gw32(g_imgbase + 0x21d58u, 0x00000258u);      /* 800 and 600 */
    gw32(SCRATCH, 0xBADF00Du);
    gw32(SCRATCH + 16u, 0xBADF00Du);
    frame(&C, args, 2);
    esp0 = C.esp;
    if (!call_body(0x10006280u, &C)) { printf("    FAIL  not in table\n"); fails++; return; }
    check("*out_width  <- [0x10021d54]", gr32(SCRATCH), 0x320u);
    check("*out_height <- [0x10021d58]", gr32(SCRATCH + 16u), 0x258u);
    check("esp delta (RET 8 = 4+8)", C.esp - esp0, 12u);
}

static void case_findmouse(void)
{
    CPU C; uint32_t args[2] = { SCRATCH, 0 }, esp0; int i;
    /* four dwords from [arg0+0x14 ..] into four image globals; RET 8 */
    printf("  0x100051c0 igWin32ControllerManager::findMouse\n");
    for (i = 0; i < 4; i++) gw32(SCRATCH + 0x14u + (uint32_t)i * 4u, 0xC0DE0000u + (uint32_t)i);
    for (i = 0; i < 4; i++) gw32(g_imgbase + 0x21b34u + (uint32_t)i * 4u, 0xBADF00Du);
    frame(&C, args, 2);
    esp0 = C.esp;
    /* Poisoned, or "eax is 0" would be true before the body ran as well --
       --selftest caught exactly that: 13 of 14 checks bound, this one did not. */
    C.eax = 0x11223344u;
    if (!call_body(0x100051c0u, &C)) { printf("    FAIL  not in table\n"); fails++; return; }
    for (i = 0; i < 4; i++) {
        char lbl[64];
        snprintf(lbl, sizeof lbl, "[imgbase+0x%x] <- [obj+0x%x]",
                 0x21b34u + (unsigned)i * 4u, 0x14u + (unsigned)i * 4u);
        check(lbl, gr32(g_imgbase + 0x21b34u + (uint32_t)i * 4u), 0xC0DE0000u + (uint32_t)i);
    }
    check("eax after XOR EAX,EAX", C.eax, 0u);
    check("esp delta (RET 8 = 4+8)", C.esp - esp0, 12u);
}

static void case_arkinit(void)
{
    CPU C;
    /* EAX = [0x10021b80] ; [EAX+0x3c] = 0x10002370 (image-relative immediate) */
    printf("  0x10002cc0 igWindow::arkRegisterInitialize\n");
    gw32(g_imgbase + 0x21b80u, SCRATCH);          /* the class meta object */
    gw32(SCRATCH + 0x3cu, 0xBADF00Du);
    frame(&C, NULL, 0);
    if (!call_body(0x10002cc0u, &C)) { printf("    FAIL  not in table\n"); fails++; return; }
    check("eax <- [0x10021b80]", C.eax, SCRATCH);
    /* The point of this one: an immediate holding an image address must be
       REBASED to where we mapped it, not emitted as the literal 0x10002370. */
    check("[meta+0x3c] rebased immediate", gr32(SCRATCH + 0x3cu), g_imgbase + 0x2370u);
}

/*
 * The import ABI, which is the easiest thing here to get quietly wrong.
 *
 * A recompiled body calls an import with a fake return address on the guest
 * stack; the implementation has to leave ESP where the real callee would.
 * Win32 is __stdcall (callee pops the arguments), the CRT is __cdecl (it does
 * not). Confusing the two shifts the guest stack by a word per call and the
 * damage shows up somewhere with no connection to the cause, so both are
 * checked here against a known-answer call rather than reasoned about.
 */
void imp_KERNEL32_GetModuleHandleA(CPU *C);
void imp_KERNEL32_MultiByteToWideChar(CPU *C);
void imp_MSVCRT_malloc(CPU *C);
void imp_MSVCRT_free(CPU *C);

/* Call an import the way a recompiled body does. Returns the ESP delta. */
static uint32_t call_import(void (*fn)(CPU *), CPU *C, const uint32_t *args,
                            int nargs)
{
    uint32_t esp0;
    int i;
    C->esp = guest_stack_top - (uint32_t)(nargs + 1) * 4u;
    gw32(C->esp, 0xDEADBEEFu);                     /* the fake return address */
    for (i = 0; i < nargs; i++) gw32(C->esp + 4u + (uint32_t)i * 4u, args[i]);
    esp0 = C->esp;
    if (!skip_body) fn(C);
    return C->esp - esp0;
}

static void case_import_abi(void)
{
    CPU C; uint32_t d;
    printf("  native import ABI (stdcall vs cdecl stack cleanup)\n");

    {   /* KERNEL32!GetModuleHandleA(NULL) -- __stdcall, 1 argument.
           The module's own handle IS its image base in a PE. */
        uint32_t args[1] = { 0 };
        memset(&C, 0, sizeof C);
        C.eax = 0xBADF00Du;
        d = call_import(imp_KERNEL32_GetModuleHandleA, &C, args, 1);
        check("GetModuleHandleA(NULL) -> imgbase", C.eax, g_imgbase);
        check("  stdcall esp delta (4+1*4)", d, 8u);
    }
    {   /* KERNEL32!MultiByteToWideChar -- __stdcall, 6 arguments. Measuring
           form first (cchWideChar 0 returns the length needed). */
        static const char msg[] = "SDL3";
        uint32_t src = SCRATCH + 0x100u, dst = SCRATCH + 0x200u;
        uint32_t args[6] = { 0, 0, 0, 0xFFFFFFFFu, 0, 0 };
        unsigned i;
        for (i = 0; i < sizeof msg; i++)
            *(volatile uint8_t *)(uintptr_t)(src + i) = (uint8_t)msg[i];
        args[2] = src; args[4] = dst;
        memset(&C, 0, sizeof C);
        C.eax = 0xBADF00Du;
        d = call_import(imp_KERNEL32_MultiByteToWideChar, &C, args, 6);
        check("MB2WC measure -> strlen+1", C.eax, 5u);
        check("  stdcall esp delta (4+6*4)", d, 28u);

        args[5] = 16;                              /* now actually convert */
        for (i = 0; i < 8; i++) gw32(dst + i * 4u, 0xBADF00Du);
        memset(&C, 0, sizeof C);
        d = call_import(imp_KERNEL32_MultiByteToWideChar, &C, args, 6);
        check("MB2WC convert -> count", C.eax, 5u);
        check("  wide 'S'", *(volatile uint16_t *)(uintptr_t)dst, (uint16_t)'S');
        check("  wide 'D'", *(volatile uint16_t *)(uintptr_t)(dst + 2u), (uint16_t)'D');
        check("  wide NUL terminator", *(volatile uint16_t *)(uintptr_t)(dst + 8u), 0u);
    }
    {   /* MSVCRT!malloc/free -- __cdecl, so the CALLER cleans up and esp moves
           by the return address only. This is the case that would silently
           differ from the Win32 ones. */
        uint32_t args[1] = { 64 }, p;
        memset(&C, 0, sizeof C);
        C.eax = 0xBADF00Du;
        d = call_import(imp_MSVCRT_malloc, &C, args, 1);
        p = C.eax;
        check("malloc(64) != 0", p != 0u && p != 0xBADF00Du, 1u);
        check("  cdecl esp delta (4 only)", d, 4u);
        if (p && p != 0xBADF00Du) {
            args[0] = p;
            memset(&C, 0, sizeof C);
            d = call_import(imp_MSVCRT_free, &C, args, 1);
            check("  free cdecl esp delta (4)", d, 4u);
        }
    }
}

static int run_battery(void)
{
    printf("battery: recompiled bodies run natively, with real postconditions\n");
    if (pe_map_anon_low(SCRATCH, 0x1000u) != 0) {
        fprintf(stderr, "battery: could not place the scratch object -- ran "
                        "NOTHING\n");
        return 1;
    }
    if (selftest) {
        int before;
        printf("  --selftest: running the battery with every body SKIPPED.\n"
               "  Each check below MUST fail; any that passes is a check that\n"
               "  does not bind.\n");
        skip_body = 1;
        before = fails;
        case_enumerate();
        case_getscreensize();
        case_findmouse();
        case_arkinit();
        case_import_abi();
        skip_body = 0;
        if (fails - before == checks) {
            printf("\n  SELFTEST passed: all %d checks failed with the bodies\n"
                   "  skipped, so a pass below means the bodies did the work.\n\n",
                   checks);
            fails = before;
            checks = 0;
        } else {
            printf("\n  SELFTEST FAILED: only %d of %d checks noticed that the\n"
                   "  bodies never ran. Every result below is unreliable.\n\n",
                   fails - before, checks);
            return 1;
        }
    }
    case_enumerate();
    case_getscreensize();
    case_findmouse();
    case_arkinit();
    case_import_abi();
    printf("\nbattery: %d of %d check(s) FAILED\n", fails, checks);
    printf("Established: the original image maps at its own base in a 64-bit\n"
           "process, the emitted C runs there natively, image-relative\n"
           "immediates are rebased, guest stack arguments and RET N cleanup are\n"
           "right, and the native import layer gets stdcall and cdecl cleanup\n"
           "right on a known-answer call.\n"
           "NOT established: the game running. Of the 107 imports the bodies\n"
           "reach, 64 are other game modules still to be recompiled and the\n"
           "rest of the Win32 surface is unimplemented -- every one of those\n"
           "aborts by name if reached. rc-native tracks the remainder.\n");
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    PeImage img;
    const char *dll = NULL;
    int window = 1, i, rc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-window") == 0) window = 0;
        else if (strcmp(argv[i], "--selftest") == 0) selftest = 1;
        else dll = argv[i];
    }
    /* No path given: fall back to the install named by GAME_PC_DIR, the same
       variable the Wine-side harness uses. If it is unset there is nothing to
       run against, and this exits 77 -- ctest's SKIP code -- saying why. A
       skip that announces itself, rather than a pass over an empty battery. */
    if (!dll) {
        const char *dir = getenv("GAME_PC_DIR");
        static char path[4096];
        if (!dir || !*dir) {
            printf("SKIP x2native: GAME_PC_DIR is unset, so there is no "
                   "libIGDisplay.dll to map. NOTHING was checked.\n");
            return 77;
        }
        snprintf(path, sizeof path, "%s/libIGDisplay.dll", dir);
        dll = path;
    }
    if (pe_map(dll, &img) != 0) return 1;
    g_imgbase = img.base;
    g_image_lo = img.base;
    g_image_hi = img.base + img.size;
    printf("mapped %s at 0x%08x (%u bytes, %d sections)\n",
           dll, img.base, img.size, img.nsections);
    printf("function table: %d recompiled bodies linked in\n", g_fn_count);

    if (guest_stack_init() != 0) {
        fprintf(stderr, "x2native: could not place the guest stack\n");
        return 1;
    }

#ifdef X2_WITH_SDL
    if (window) {
        SDL_Window *w;
        /* SDL3 returns true on success, where SDL2 returned 0. */
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            fprintf(stderr, "x2native: SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
        w = SDL_CreateWindow("x2native", 800, 600, 0);
        if (!w) {
            fprintf(stderr, "x2native: SDL_CreateWindow failed: %s\n",
                    SDL_GetError());
            SDL_Quit();
            return 1;
        }
        printf("SDL: a real window exists; this is where the USER32/DINPUT/D3D8"
               " surface lands.\n");
        SDL_DestroyWindow(w);
        SDL_Quit();
    } else {
        printf("SDL: window skipped (--no-window)\n");
    }
#else
    (void)window;
    printf("SDL: not compiled in\n");
#endif

    printf("\n");
    rc = run_battery();
    pe_unmap(&img);
    return rc;
}
