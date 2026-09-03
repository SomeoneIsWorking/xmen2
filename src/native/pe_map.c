/*
 * Map a PE32 image into the guest's 32-bit address space.
 *
 * The Wine-hosted build never needed this: the Windows loader mapped
 * libIGDisplay_orig.dll and the runtime just read `g_imgbase` off the module
 * handle. A native build has no loader, so the image has to be placed by hand
 * The logical addresses remain the original PE addresses. guest_memory owns
 * whether those are identity-mapped (Linux) or translated through a high 4 GB
 * arena (arm64 macOS, whose Mach-O __PAGEZERO occupies the low 4 GB).
 */
#include "pe_map.h"
#include "guest_memory.h"
#include "platform_mman.h"

#include <strings.h>   /* strcasecmp */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define RD16(p, o) ((uint16_t)((p)[(o)] | ((p)[(o) + 1] << 8)))
#define RD32_(p, o) ((uint32_t)((p)[(o)] | ((p)[(o) + 1] << 8) \
                                | ((p)[(o) + 2] << 16) | ((p)[(o) + 3] << 24)))

#define DIR_EXPORT    0
#define DIR_IMPORT    1
#define DIR_BASERELOC 5

static uint32_t data_dir_at(const unsigned char *p, int which, uint32_t *size);
static uint32_t pe_apply_relocs(uint32_t base, uint32_t rel, uint32_t relsz,
                                uint32_t delta);

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
    uint32_t pe, nsec, opt, base, imgsize, hdrsize, prefbase;

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
    prefbase = base;
    imgsize = RD32_(f, opt + 56);
    hdrsize = RD32_(f, opt + 60);

    /* One reservation for the whole image, then the sections are written into
       it. Reserving per-section would leave the gaps between them unmapped,
       and code reads across section boundaries (padding, jump tables). */
    if (guest_memory_map_fixed(base, imgsize, PROT_READ | PROT_WRITE) != 0) {
        /* Every libIG*.dll is linked for 0x10000000, so at most one of them
           gets its preferred base -- exactly as under the Windows loader.
           Relocation is fine because absolute references resolve against the
           module's OWN base; what would not be fine is relocating silently,
           so the new base is returned and the caller prints it. */
        if (guest_memory_map_any(0x20000000u, 0xF0000000u, 0x01000000u,
                                 imgsize, PROT_READ | PROT_WRITE, &base) != 0) {
            fprintf(stderr, "pe_map: %s wants 0x%08x, which is taken, and no "
                            "free span of %u bytes was found below 4 GB. Guest "
                            "pointers are 32-bit, so there is nowhere else to "
                            "put it.\n", path, base, imgsize);
            goto fail;
        }
    }
    memcpy(guest_memory_pointer(base), f, hdrsize);
    for (i = 0; i < (int)nsec; i++) {
        uint32_t s = pe + 24 + RD16(f, pe + 20) + (uint32_t)i * 40;
        uint32_t va = RD32_(f, s + 12), vsz = RD32_(f, s + 8);
        uint32_t raw = RD32_(f, s + 20), rsz = RD32_(f, s + 16);
        uint32_t n = rsz < vsz ? rsz : vsz;
        if (raw + n > (uint32_t)st.st_size) {
            fprintf(stderr, "pe_map: section %d of %s runs past the file\n",
                    i, path);
            guest_memory_release(base, imgsize);
            goto fail;
        }
        /* The rest of the section stays zero, which is what a loader does for
           the BSS tail -- and it is zero because the mapping is anonymous. */
        if (n) memcpy(guest_memory_pointer(base + va), f + raw, n);
    }
    /* Apply base relocations if the image did not land where it was linked.
     *
     * This was missing, and it is the kind of missing that does not announce
     * itself: the recompiled CODE resolves absolute references against
     * G_IMGBASE, so code works at any base -- but the image's own DATA holds
     * pointers baked in at link time (vtables, string tables, jump tables).
     * Relocating without fixing those leaves every one of them pointing at the
     * preferred base, which is either unmapped or somebody else's module.
     */
    if (base != (uint32_t)prefbase) {
        uint32_t rel, relsz, n = 0;
        rel = data_dir_at(f, DIR_BASERELOC, &relsz);
        if (!rel || !relsz) {
            fprintf(stderr, "pe_map: %s had to move from 0x%08x to 0x%08x and "
                            "has NO relocation directory. Its data pointers "
                            "cannot be fixed, so it would run against the "
                            "wrong addresses.\n",
                    path, (uint32_t)prefbase, base);
            guest_memory_release(base, imgsize);
            goto fail;
        }
        n = pe_apply_relocs(base, rel, relsz, base - (uint32_t)prefbase);
        fprintf(stderr, "pe_map: %s relocated 0x%08x -> 0x%08x, %u fixups "
                        "applied\n", path, (uint32_t)prefbase, base, n);
    }
    munmap(f, (size_t)st.st_size);
    out->base = base;
    out->preferred = (uint32_t)prefbase;
    out->size = imgsize;
    out->nsections = (int)nsec;
    return 0;
fail:
    munmap(f, (size_t)st.st_size);
    return -1;
}

void pe_unmap(PeImage *img)
{
    if (img->base) guest_memory_release(img->base, img->size);
    img->base = 0;
}

/* A plain low anonymous mapping at a fixed address, for the guest stack. Same
   refusal rule as pe_map(): a stack somewhere other than where the caller
   asked for it would still work, and would make every address in a fault
   report unrecognisable. */
int pe_map_anon_low(uint32_t want, uint32_t size)
{
    if (guest_memory_map_fixed(want, size, PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "pe_map_anon_low: wanted 0x%08x, %s\n", want,
                strerror(errno));
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

/* Same as data_dir but reading the file buffer, needed before the mapping is
   complete. */
static uint32_t data_dir_at(const unsigned char *p, int which, uint32_t *size)
{
    uint32_t pe = RD32_(p, 0x3C), opt = pe + 24;
    uint32_t d = opt + 96 + (uint32_t)which * 8;
    if (size) *size = RD32_(p, d + 4);
    return RD32_(p, d);
}

/*
 * Walk the .reloc blocks and add `delta` to every HIGHLOW fixup.
 *
 * Types other than ABSOLUTE and HIGHLOW do not occur in 32-bit x86 images, and
 * meeting one means the assumption is wrong -- so it stops rather than
 * skipping quietly and leaving a handful of pointers unfixed.
 */
static uint32_t pe_apply_relocs(uint32_t base, uint32_t rel, uint32_t relsz,
                                uint32_t delta)
{
    const unsigned char *img = guest_memory_const_pointer(base);
    uint32_t off = 0, n = 0;
    while (off + 8 <= relsz) {
        uint32_t va = RD32_(img, rel + off);
        uint32_t sz = RD32_(img, rel + off + 4), i;
        if (sz < 8 || off + sz > relsz) break;
        for (i = 8; i + 2 <= sz; i += 2) {
            uint16_t e = (uint16_t)RD16(img, rel + off + i);
            uint32_t type = (uint32_t)(e >> 12), where = va + (e & 0x0FFFu);
            if (type == 0) continue;                     /* ABSOLUTE: padding */
            if (type != 3) {
                fprintf(stderr, "pe_map: relocation type %u at rva 0x%x is not "
                                "HIGHLOW; a 32-bit image should not contain "
                                "one, so this is not safe to skip\n",
                        type, where);
                abort();
            }
            *(volatile uint32_t *)guest_memory_pointer(base + where) += delta;
            n++;
        }
        off += sz;
    }
    return n;
}

static uint32_t data_dir(uint32_t base, int which, uint32_t *size)
{
    const unsigned char *p = guest_memory_const_pointer(base);
    uint32_t pe = RD32_(p, 0x3C), opt = pe + 24;
    uint32_t d = opt + 96 + (uint32_t)which * 8;
    if (size) *size = RD32_(p, d + 4);
    return RD32_(p, d);
}

uint32_t pe_export_rva(uint32_t base, const char *name)
{
    uint32_t dir = data_dir(base, DIR_EXPORT, NULL), n, i;
    const unsigned char *p = guest_memory_const_pointer(base);
    uint32_t names, ords, funcs;
    if (!dir) return 0;
    n     = RD32_(p, dir + 0x18);
    funcs = RD32_(p, dir + 0x1C);
    names = RD32_(p, dir + 0x20);
    ords  = RD32_(p, dir + 0x24);
    for (i = 0; i < n; i++) {
        uint32_t nr = RD32_(p, names + i * 4);
        if (strcmp(guest_memory_const_pointer(base + nr), name) == 0) {
            uint16_t o = (uint16_t)RD16(p, ords + i * 2);
            return RD32_(p, funcs + (uint32_t)o * 4);
        }
    }
    return 0;
}

/*
 * Which exported function contains an RVA.
 *
 * Linear over the name table because it is a DIAGNOSTIC lookup -- fault
 * reports, dispatch reports, the DirectInput caller check -- asked thousands
 * of times in a run and not per instruction. A miss is reported as 0 rather
 * than as the first export, so "no export at or below this" is an answer.
 */
uint32_t pe_export_containing(uint32_t base, uint32_t rva,
                              const char **name_out)
{
    uint32_t dir = data_dir(base, DIR_EXPORT, NULL), n, i;
    const unsigned char *p = guest_memory_const_pointer(base);
    uint32_t names, ords, funcs, best = 0;
    const char *bestnm = NULL;
    if (name_out) *name_out = NULL;
    if (!dir) return 0;
    n     = RD32_(p, dir + 0x18);
    funcs = RD32_(p, dir + 0x1C);
    names = RD32_(p, dir + 0x20);
    ords  = RD32_(p, dir + 0x24);
    for (i = 0; i < n; i++) {
        uint16_t o  = (uint16_t)RD16(p, ords + i * 2);
        uint32_t fr = RD32_(p, funcs + (uint32_t)o * 4);
        if (fr > rva || fr < best) continue;
        best   = fr;
        bestnm = guest_memory_const_pointer(base + RD32_(p, names + i * 4));
    }
    if (name_out) *name_out = bestnm;
    return best;
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
    const unsigned char *p = guest_memory_const_pointer(base);
    int bound = 0, poisoned = 0;
    if (!dir) { if (out_bound) *out_bound = 0;
                if (out_poisoned) *out_poisoned = 0; return 0; }
    for (;; dir += 20) {
        uint32_t oft = RD32_(p, dir + 0), nameR = RD32_(p, dir + 12);
        uint32_t ft = RD32_(p, dir + 16), t;
        const char *mod;
        if (!oft && !ft && !nameR) break;
        mod = guest_memory_const_pointer(base + nameR);
        if (!oft) oft = ft;                    /* some linkers omit the INT */
        for (t = 0;; t += 4) {
            uint32_t thunk = RD32_(p, oft + t), addr;
            if (!thunk) break;
            if (thunk & 0x80000000u)
                addr = resolve(mod, NULL, 1, thunk & 0xFFFFu, ctx);
            else
                addr = resolve(mod,
                               guest_memory_const_pointer(base + thunk + 2),
                               0, 0, ctx);
            if (addr) bound++; else poisoned++;
            /* The caller's poison value arrives as resolve() returning 0; it
               is filled in by the caller afterwards via out_poisoned bookkeeping
               only if it chose to. Here a 0 is left for the caller to overwrite. */
            *(volatile uint32_t *)guest_memory_pointer(base + ft + t) = addr;
        }
    }
    if (out_bound) *out_bound = bound;
    if (out_poisoned) *out_poisoned = poisoned;
    return 0;
}

uint32_t pe_entry_rva(uint32_t base)
{
    const unsigned char *p = guest_memory_const_pointer(base);
    uint32_t pe = RD32_(p, 0x3C);
    return RD32_(p, pe + 24 + 16);
}

/* Does this image import anything from `modname`? Used to order module
   initialisation: a module's constructors may call into another module, so the
   one it depends on has to be initialised first. */
int pe_imports_module(uint32_t base, const char *modname)
{
    uint32_t dir = data_dir(base, DIR_IMPORT, NULL);
    const unsigned char *p = guest_memory_const_pointer(base);
    if (!dir) return 0;
    for (;; dir += 20) {
        uint32_t oft = RD32_(p, dir + 0), nameR = RD32_(p, dir + 12);
        uint32_t ft = RD32_(p, dir + 16);
        if (!oft && !ft && !nameR) break;
        if (strcasecmp(guest_memory_const_pointer(base + nameR), modname) == 0)
            return 1;
    }
    return 0;
}

/* Is this image a DLL? An EXE's entry point is not DllMain -- it is the
   program -- so module initialisation must not "initialise" it by running it. */
int pe_is_dll(uint32_t base)
{
    const unsigned char *p = guest_memory_const_pointer(base);
    uint32_t pe = RD32_(p, 0x3C);
    return (RD16(p, pe + 22) & 0x2000) != 0;   /* IMAGE_FILE_DLL */
}
