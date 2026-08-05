/*
 * Map a PE32 image into this process at its OWN preferred base.
 *
 * The Wine-hosted build never needed this: the Windows loader mapped
 * libIGDisplay_orig.dll and the runtime just read `g_imgbase` off the module
 * handle. A native build has no loader, so the image has to be placed by hand
 * -- and placed at the base it was linked for, because the recompiled bodies
 * dereference guest addresses directly (`RD32(a)` is `*(uint32_t *)a`).
 *
 * That works in a 64-bit process because every PE base in this game is below
 * 4 GB: 0x00400000 for XMen2.exe, 0x10000000 for the DLLs. MAP_FIXED_NOREPLACE
 * asks for exactly that address and REFUSES rather than relocating, which is
 * the whole point -- a silently relocated image would read as "the recompiled
 * code is wrong" when it is the mapping that is wrong.
 *
 * Relocations are therefore never applied: the image is at its preferred base
 * or the mapping failed. If that ever stops being true, the .reloc section has
 * to be processed and this comment is the reason it was not.
 */
#include "pe_map.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define RD16(p, o) ((uint16_t)((p)[(o)] | ((p)[(o) + 1] << 8)))
#define RD32_(p, o) ((uint32_t)((p)[(o)] | ((p)[(o) + 1] << 8) \
                                | ((p)[(o) + 2] << 16) | ((p)[(o) + 3] << 24)))

/* Map at the image's own preferred base if `want` matches it and that address
   is free; otherwise place it anywhere below 4 GB and report the relocation.
   Relocating is legitimate here in a way it was not before: absolute
   references are emitted against each module's OWN base, so a module that
   moves still resolves correctly. What is NOT legitimate is moving it and not
   saying so, which is why the caller is told and prints it. */
int pe_map_at(const char *path, uint32_t want, PeImage *out)
{
    (void)want;
    return pe_map(path, out);
}

int pe_map(const char *path, PeImage *out)
{
    unsigned char *f;
    struct stat st;
    int fd, i;
    uint32_t pe, nsec, opt, base, imgsize, hdrsize;
    void *got;

    memset(out, 0, sizeof *out);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "pe_map: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fstat(fd, &st) < 0 || st.st_size < 0x40) {
        fprintf(stderr, "pe_map: %s is not a file with a DOS header\n", path);
        close(fd);
        return -1;
    }
    f = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (f == MAP_FAILED) {
        fprintf(stderr, "pe_map: cannot map %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (f[0] != 'M' || f[1] != 'Z') {
        fprintf(stderr, "pe_map: %s has no MZ signature\n", path);
        goto fail;
    }
    pe = RD32_(f, 0x3C);
    if (pe + 0x78 > (uint32_t)st.st_size || memcmp(f + pe, "PE\0\0", 4) != 0) {
        fprintf(stderr, "pe_map: %s has no PE header at 0x%x\n", path, pe);
        goto fail;
    }
    nsec = RD16(f, pe + 6);
    opt = pe + 24;
    if (RD16(f, opt) != 0x10B) {
        fprintf(stderr, "pe_map: %s is not PE32 (optional magic 0x%x); this "
                        "recompiler targets 32-bit images only\n",
                path, RD16(f, opt));
        goto fail;
    }
    base = RD32_(f, opt + 28);
    imgsize = RD32_(f, opt + 56);
    hdrsize = RD32_(f, opt + 60);

    /* One reservation for the whole image, then the sections are written into
       it. Reserving per-section would leave the gaps between them unmapped,
       and code reads across section boundaries (padding, jump tables). */
    got = mmap((void *)(uintptr_t)base, imgsize, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (got == MAP_FAILED || (uintptr_t)got != (uintptr_t)base) {
        /* Every libIG*.dll is linked for 0x10000000, so at most one of them
           gets its preferred base -- exactly as under the Windows loader.
           Relocation is fine because absolute references resolve against the
           module's OWN base; what would not be fine is relocating silently,
           so the new base is returned and the caller prints it. */
        uint64_t cand;
        if (got != MAP_FAILED) munmap(got, imgsize);
        /* Search low addresses explicitly. Letting the kernel choose (mmap
           with a NULL hint) returns a 64-bit address in a 64-bit process --
           measured, on the very first two-module run -- and guest pointers are
           32 bits, so anything above 4 GB is unusable however well it maps. */
        got = MAP_FAILED;
        for (cand = 0x20000000ull; cand + imgsize < 0xF0000000ull;
             cand += 0x01000000ull) {
            void *t = mmap((void *)(uintptr_t)cand, imgsize,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                           -1, 0);
            if (t != MAP_FAILED && (uintptr_t)t == (uintptr_t)cand) { got = t; break; }
            if (t != MAP_FAILED) munmap(t, imgsize);
        }
        if (got == MAP_FAILED) {
            fprintf(stderr, "pe_map: %s wants 0x%08x, which is taken, and no "
                            "free span of %u bytes was found below 4 GB. Guest "
                            "pointers are 32-bit, so there is nowhere else to "
                            "put it.\n", path, base, imgsize);
            goto fail;
        }
        base = (uint32_t)(uintptr_t)got;
    }
    memcpy((void *)(uintptr_t)base, f, hdrsize);
    for (i = 0; i < (int)nsec; i++) {
        uint32_t s = pe + 24 + RD16(f, pe + 20) + (uint32_t)i * 40;
        uint32_t va = RD32_(f, s + 12), vsz = RD32_(f, s + 8);
        uint32_t raw = RD32_(f, s + 20), rsz = RD32_(f, s + 16);
        uint32_t n = rsz < vsz ? rsz : vsz;
        if (raw + n > (uint32_t)st.st_size) {
            fprintf(stderr, "pe_map: section %d of %s runs past the file\n",
                    i, path);
            munmap((void *)(uintptr_t)base, imgsize);
            goto fail;
        }
        /* The rest of the section stays zero, which is what a loader does for
           the BSS tail -- and it is zero because the mapping is anonymous. */
        if (n) memcpy((void *)(uintptr_t)(base + va), f + raw, n);
    }
    munmap(f, (size_t)st.st_size);
    out->base = base;
    out->size = imgsize;
    out->nsections = (int)nsec;
    return 0;
fail:
    munmap(f, (size_t)st.st_size);
    return -1;
}

void pe_unmap(PeImage *img)
{
    if (img->base) munmap((void *)(uintptr_t)img->base, img->size);
    img->base = 0;
}

/* A plain low anonymous mapping at a fixed address, for the guest stack. Same
   refusal rule as pe_map(): a stack somewhere other than where the caller
   asked for it would still work, and would make every address in a fault
   report unrecognisable. */
int pe_map_anon_low(uint32_t want, uint32_t size)
{
    void *got = mmap((void *)(uintptr_t)want, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (got == MAP_FAILED || (uintptr_t)got != (uintptr_t)want) {
        fprintf(stderr, "pe_map_anon_low: wanted 0x%08x, %s\n", want,
                got == MAP_FAILED ? strerror(errno) : "got somewhere else");
        if (got != MAP_FAILED) munmap(got, size);
        return -1;
    }
    return 0;
}

/* ---- export and import directories of a MAPPED image -------------------
 *
 * The hosted build never needed these: the Windows loader resolved libIGCore's
 * exports into libIGDisplay's IAT before any game code ran. Natively nobody
 * does, and the consequence is not a link error -- it is silence. 40 of
 * libIGDisplay's IAT slots are READ AS DATA in 113 places in the emitted code
 * (they are data exports: ArkCore, kSuccess, the memory-pool adaptor). In a
 * file image those slots still hold hint/name RVAs, so every one of those
 * reads would return a small integer that looks like a pointer.
 */
#define DIR_EXPORT 0
#define DIR_IMPORT 1

static uint32_t data_dir(uint32_t base, int which, uint32_t *size)
{
    const unsigned char *p = (const unsigned char *)(uintptr_t)base;
    uint32_t pe = RD32_(p, 0x3C), opt = pe + 24;
    uint32_t d = opt + 96 + (uint32_t)which * 8;
    if (size) *size = RD32_(p, d + 4);
    return RD32_(p, d);
}

uint32_t pe_export_rva(uint32_t base, const char *name)
{
    uint32_t dir = data_dir(base, DIR_EXPORT, NULL), n, i;
    const unsigned char *p = (const unsigned char *)(uintptr_t)base;
    uint32_t names, ords, funcs;
    if (!dir) return 0;
    n     = RD32_(p, dir + 0x18);
    funcs = RD32_(p, dir + 0x1C);
    names = RD32_(p, dir + 0x20);
    ords  = RD32_(p, dir + 0x24);
    for (i = 0; i < n; i++) {
        uint32_t nr = RD32_(p, names + i * 4);
        if (strcmp((const char *)(uintptr_t)(base + nr), name) == 0) {
            uint16_t o = (uint16_t)RD16(p, ords + i * 2);
            return RD32_(p, funcs + (uint32_t)o * 4);
        }
    }
    return 0;
}

/*
 * Bind every import slot, the way a loader would.
 *
 * `resolve` returns the address to write, or 0 when it cannot resolve one. A 0
 * is NOT written as 0: the caller supplies a poison address instead, so that a
 * slot nobody could resolve faults the moment it is used rather than reading as
 * NULL and taking a plausible-looking early-out branch.
 */
int pe_bind_imports(uint32_t base,
                    uint32_t (*resolve)(const char *mod, const char *sym,
                                        int by_ordinal, uint32_t ordinal,
                                        void *ctx),
                    void *ctx, int *out_bound, int *out_poisoned)
{
    uint32_t dir = data_dir(base, DIR_IMPORT, NULL);
    const unsigned char *p = (const unsigned char *)(uintptr_t)base;
    int bound = 0, poisoned = 0;
    if (!dir) { if (out_bound) *out_bound = 0;
                if (out_poisoned) *out_poisoned = 0; return 0; }
    for (;; dir += 20) {
        uint32_t oft = RD32_(p, dir + 0), nameR = RD32_(p, dir + 12);
        uint32_t ft = RD32_(p, dir + 16), t;
        const char *mod;
        if (!oft && !ft && !nameR) break;
        mod = (const char *)(uintptr_t)(base + nameR);
        if (!oft) oft = ft;                    /* some linkers omit the INT */
        for (t = 0;; t += 4) {
            uint32_t thunk = RD32_(p, oft + t), addr;
            if (!thunk) break;
            if (thunk & 0x80000000u)
                addr = resolve(mod, NULL, 1, thunk & 0xFFFFu, ctx);
            else
                addr = resolve(mod,
                               (const char *)(uintptr_t)(base + thunk + 2),
                               0, 0, ctx);
            if (addr) bound++; else poisoned++;
            /* The caller's poison value arrives as resolve() returning 0; it
               is filled in by the caller afterwards via out_poisoned bookkeeping
               only if it chose to. Here a 0 is left for the caller to overwrite. */
            *(volatile uint32_t *)(uintptr_t)(base + ft + t) = addr;
        }
    }
    if (out_bound) *out_bound = bound;
    if (out_poisoned) *out_poisoned = poisoned;
    return 0;
}
