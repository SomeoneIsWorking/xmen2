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
        fprintf(stderr, "pe_map: %s wants base 0x%08x and the kernel %s.\n"
                        "  Not relocating: the recompiled code dereferences "
                        "guest addresses directly, so an image anywhere else "
                        "is not the image.\n",
                path, base,
                got == MAP_FAILED ? strerror(errno) : "put it elsewhere");
        if (got != MAP_FAILED) munmap(got, imgsize);
        goto fail;
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
