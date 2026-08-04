/* Host runner for the recompiled XMen2.exe.
 *
 * The recompiled code is native C, but it still needs the original image's
 * DATA: globals, string literals, vtables, jump tables and the import table all
 * live in XMen2.exe's .rdata/.data, and the emitted code addresses them as
 * G_IMGBASE + RVA. So this maps the original file's sections at their preferred
 * base and points G_IMGBASE at them. Nothing of the original's CODE is
 * executed -- .text is mapped only because addresses within it are referenced
 * (jump tables, function pointers) and because leaving a hole there would turn
 * a stray reference into a fault with no diagnosis.
 *
 * This runner must NOT itself occupy 0x00400000, so it is linked elsewhere
 * (see --image-base in the build line).
 *
 * Every failure path here reports what was being attempted. A loader that
 * half-works produces a crash hundreds of thousands of instructions later.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "x86rt.h"

int  x86_resolve_imports(void);
void fn_006725f4(CPU *C);          /* XMen2.exe entry point */

#define GUEST_STACK 0x100000

/* XMen2.exe has NO relocation directory, so absolute pointers baked into its
   data (vtables, string tables, jump tables) are only valid at 0x00400000. The
   image therefore cannot be rebased -- it must be mapped exactly there.
   By the time main() runs, Wine's low-memory reservation and this runner's own
   CRT heap already occupy that range, so the region is claimed here, in a
   constructor that runs before the heap has a chance to grow into it. */
static void *g_guest_region;
static uint32_t g_guest_base, g_guest_size;

__attribute__((constructor(101)))
static void reserve_guest_image(void)
{
    /* Wine reserves the classic exe slot; release it before reserving ours. */
    VirtualFree((LPVOID)0x00400000, 0, MEM_RELEASE);
    g_guest_base = 0x00400000;
    g_guest_size = 0x00700000;          /* covers XMen2.exe's 0x674000 image */
    g_guest_region = VirtualAlloc((LPVOID)(uintptr_t)g_guest_base, g_guest_size,
                                  MEM_RESERVE, PAGE_NOACCESS);
}

static void die(const char *what)
{
    fprintf(stderr, "x2run: %s (err %lu)\n", what, (unsigned long)GetLastError());
    exit(2);
}

/* Map the original image at its preferred base. */
static void map_image(const char *path)
{
    HANDLE f;
    DWORD got, size;
    uint8_t *raw, *base;
    uint32_t peoff, nsec, optsz, i, imgbase, imgsize;

    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) die("cannot open the original XMen2.exe");
    size = GetFileSize(f, NULL);
    raw = (uint8_t *)malloc(size);
    if (!raw || !ReadFile(f, raw, size, &got, NULL) || got != size)
        die("cannot read the original XMen2.exe");
    CloseHandle(f);

    if (raw[0] != 'M' || raw[1] != 'Z') die("original is not an MZ image");
    peoff = *(uint32_t *)(raw + 0x3C);
    optsz = *(uint16_t *)(raw + peoff + 4 + 16);
    nsec  = *(uint16_t *)(raw + peoff + 4 + 2);
    imgbase = *(uint32_t *)(raw + peoff + 4 + 20 + 28);
    imgsize = *(uint32_t *)(raw + peoff + 4 + 20 + 56);

    {   /* Linking this runner at 0x00400000 with its own sections placed above
           the guest image makes Wine map the whole range as OUR image. The low
           part is then already committed (MEM_IMAGE, execute-writecopy), so it
           must be re-protected and written into rather than allocated -- a
           VirtualAlloc over MEM_IMAGE returns ACCESS_DENIED. */
        MEMORY_BASIC_INFORMATION q;
        if (VirtualQuery((LPCVOID)(uintptr_t)imgbase, &q, sizeof q) &&
            q.State == MEM_COMMIT && q.Type == MEM_IMAGE) {
            DWORD old;
            if (!VirtualProtect((LPVOID)(uintptr_t)imgbase, imgsize,
                                PAGE_EXECUTE_READWRITE, &old))
                die("cannot make the guest image range writable");
            base = (uint8_t *)(uintptr_t)imgbase;
            memcpy(base, raw, 0x1000);
            for (i = 0; i < nsec; i++) {
                uint8_t *sh = raw + peoff + 4 + 20 + optsz + 40 * i;
                uint32_t vaddr = *(uint32_t *)(sh + 12);
                uint32_t vsize = *(uint32_t *)(sh + 8);
                uint32_t rsize = *(uint32_t *)(sh + 16);
                uint32_t raddr = *(uint32_t *)(sh + 20);
                /* zero first: this memory is OUR image's, not fresh pages, so
                   the BSS tail is not implicitly zero the way VirtualAlloc is */
                memset(base + vaddr, 0, vsize);
                if (rsize) memcpy(base + vaddr, raw + raddr,
                                  rsize < vsize ? rsize : vsize);
            }
            free(raw);
            g_imgbase = imgbase;
            g_image_lo = imgbase;
            g_image_hi = imgbase + imgsize;
            printf("x2run: guest image written into the runner's own reserved "
                   "range at 0x%08x (%u bytes), %u sections\n",
                   imgbase, imgsize, nsec);
            return;
        }
    }
    if (!g_guest_region)
        fprintf(stderr, "x2run: WARNING the early reservation of 0x%08x failed; "
                "the commit below will probably fail too\n", 0x00400000);
    if (imgsize > g_guest_size)
        fprintf(stderr, "x2run: WARNING image is 0x%x but only 0x%x was "
                "reserved\n", imgsize, g_guest_size);
    base = (uint8_t *)VirtualAlloc((LPVOID)(uintptr_t)imgbase, imgsize,
                                   MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!base) {
        /* Say WHAT is in the way; "cannot reserve" alone is unactionable. */
        MEMORY_BASIC_INFORMATION mbi;
        uint32_t a = imgbase;
        fprintf(stderr, "x2run: cannot reserve 0x%08x..0x%08x (err %lu). "
                "Occupancy of that range:\n",
                imgbase, imgbase + imgsize, (unsigned long)GetLastError());
        while (a < imgbase + imgsize &&
               VirtualQuery((LPCVOID)(uintptr_t)a, &mbi, sizeof mbi)) {
            fprintf(stderr, "    0x%08x + 0x%-8x state=%s prot=0x%lx type=0x%lx\n",
                    (unsigned)(uintptr_t)mbi.BaseAddress, (unsigned)mbi.RegionSize,
                    mbi.State == MEM_FREE ? "FREE  " :
                    mbi.State == MEM_RESERVE ? "RESERV" : "COMMIT",
                    (unsigned long)mbi.Protect, (unsigned long)mbi.Type);
            if (!mbi.RegionSize) break;
            a = (uint32_t)((uintptr_t)mbi.BaseAddress + mbi.RegionSize);
        }
        exit(2);
    }
    /* headers, then each section; VirtualAlloc already zeroed the BSS tail */
    memcpy(base, raw, 0x1000);
    for (i = 0; i < nsec; i++) {
        uint8_t *sh = raw + peoff + 4 + 20 + optsz + 40 * i;
        uint32_t vaddr = *(uint32_t *)(sh + 12);
        uint32_t rsize = *(uint32_t *)(sh + 16);
        uint32_t raddr = *(uint32_t *)(sh + 20);
        if (rsize) memcpy(base + vaddr, raw + raddr, rsize);
    }
    free(raw);
    g_imgbase = imgbase;
    printf("x2run: mapped original image at 0x%08x (%u bytes), %u sections\n",
           imgbase, imgsize, nsec);
}

#ifdef X86_TRACE_CALLS
/* Is it looping or blocked? "Nothing renders" cannot distinguish the two, and
   the answer decides where to look next: a climbing call count means guest code
   is spinning, a static one means it is parked inside a host call. */
static DWORD WINAPI watchdog(LPVOID p)
{
    unsigned long last = 0;
    int i;
    (void)p;
    for (i = 0; i < 60; i++) {
        unsigned long now;
        Sleep(5000);
        now = x86_fn_calls;
        fprintf(stderr, "watchdog: %lu recompiled calls (+%lu in 5s) -- %s\n",
                now, now - last, now == last ? "BLOCKED in host code"
                                             : "guest code is running");
        if (now != last) x86_dump_history();
        last = now;
    }
    return 0;
}
#endif

int main(int argc, char **argv)
{
    static uint8_t *stack;
    CPU C;
    uint32_t sp;
    const char *img = (argc > 1) ? argv[1] : "XMen2_orig.exe";

    map_image(img);

    if (x86_resolve_imports() != 0) {
        fprintf(stderr, "x2run: imports unresolved -- refusing to start, "
                        "because a NULL import aborts far from its cause\n");
        return 2;
    }
    printf("x2run: imports resolved\n");

    stack = (uint8_t *)VirtualAlloc(NULL, GUEST_STACK, MEM_COMMIT, PAGE_READWRITE);
    if (!stack) die("cannot allocate the guest stack");

    memset(&C, 0, sizeof C);
    sp = (uint32_t)(uintptr_t)(stack + GUEST_STACK - 0x1000);
    *(uint32_t *)(uintptr_t)sp = 0xDEADBEEFU;      /* return address */
    C.esp = sp;
    C.fcw = 0x027FU;                                /* x87 default control word */

    /* Hybrid: addresses static analysis never resolved into functions run as
       original code. Each one is reported, and the summary at exit is the
       honest measure of how much is still not recompiled. */
    x86_allow_fallback = (getenv("X2_NO_FALLBACK") == NULL);
    printf("x2run: hybrid fallback %s\n",
           x86_allow_fallback ? "ENABLED (untranslated targets run original code)"
                              : "disabled");
    atexit(x86_fallback_report);
#ifdef X86_TRACE_CALLS
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
#endif
    printf("x2run: entering recompiled XMen2.exe at 0x006725f4\n");
    fflush(stdout);
    fn_006725f4(&C);
    printf("x2run: returned from the entry point, eax=0x%08x\n", C.eax);
    return 0;
}
