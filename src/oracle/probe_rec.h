/*
 * The oracle record stream: ONE format, written by BOTH sides.
 *
 * Everything this project has learned about the port so far was settled by
 * comparing the port against the stock game running under Wine, and every one
 * of those comparisons was built by hand for a single question -- the caps
 * dump, then the vertex dump, then the vertex-shader constants. Each was
 * right, each took a day, and each could only ever answer the one thing it was
 * cut for. tools/constcmp.py found that the bone palette is wrong (issue #80)
 * and then had nothing to say about WHERE, because it can only see the values
 * that reach D3D8.
 *
 * This is the general form. A probe names a guest FUNCTION; both sides record
 * its arguments and its results at every call; the two streams are diffed and
 * the FIRST call whose output differs is the defect, located. The port is
 * hooked with ld --wrap (the mechanism src/native/overrides.json already uses,
 * with the same isolate requirement) and the stock game is hooked in-process
 * by tools/proxy_d3d8, which the engine already loads as its d3d8.
 *
 * WHY ONE HEADER. The two recorders are compiled by different compilers for
 * different architectures -- 32-bit mingw for the stock side, 64-bit ELF for
 * the port -- and a comparison is worth nothing if the two sides disagree
 * about the layout by one byte. So the format lives here once, the field
 * tables are GENERATED from tools/probes.json into probe_table.h, and a stream
 * carries the hash of the manifest that produced it. oraclediff.py refuses two
 * streams whose hashes differ rather than reporting a difference that is
 * really a manifest edit.
 *
 * Nothing here allocates and nothing here is C99-or-later, because the stock
 * side runs inside the game's process with its own CRT.
 */
#ifndef PROBE_REC_H
#define PROBE_REC_H

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
typedef unsigned char      pr_u8;
typedef unsigned short     pr_u16;
typedef unsigned int       pr_u32;
typedef unsigned long long pr_u64;
typedef unsigned int       pr_uptr;    /* 32-bit in the game's own process */
#else
#include <stdint.h>
#include <stdbool.h>
typedef uintptr_t pr_uptr;
typedef uint8_t  pr_u8;
typedef uint16_t pr_u16;
typedef uint32_t pr_u32;
typedef uint64_t pr_u64;
#endif

/* Bumped when the layout below changes; a stream with the wrong magic is
   refused by name rather than parsed into nonsense. */
#define PROBE_MAGIC   "X2PROBE1"
#define PROBE_MAGIC_N 8

#define PROBE_SIDE_PORT  0
#define PROBE_SIDE_STOCK 1

/* A field is read either BEFORE the call (the arguments) or AFTER it (what the
   function wrote). `src` says where the dword lives at ENTRY -- the two sides
   both save ECX and the argument base on entry, so an `out` field still refers
   to the entry value even after the callee has clobbered the register. */
#define PROBE_WHEN_IN  0
#define PROBE_WHEN_OUT 1

#define PROBE_SRC_ECX   0     /* the this-pointer of a __thiscall */
#define PROBE_SRC_STACK 1     /* a dword at <first argument> + off */

#define PROBE_KIND_DWORD 0    /* record the dword itself */
#define PROBE_KIND_DEREF 1    /* record `len` bytes AT the dword */

typedef struct {
    pr_u8       when;
    pr_u8       src;
    pr_u8       kind;
    pr_u8       pad;
    pr_u16      off;          /* PROBE_SRC_STACK only */
    pr_u16      len;          /* bytes recorded: 4 for a dword */
    const char *name;
} ProbeField;

typedef struct {
    pr_u32            id;
    const char       *name;    /* the qualified guest name, for reports */
    const char       *module;  /* "libIGMath" -- no extension */
    pr_u32            linked;  /* address at the module's PREFERRED base */
    pr_u16            prologue;/* bytes the stock-side hook relocates */
    pr_u16            nfields;
    const ProbeField *fields;
    const pr_u8      *expect;  /* the prologue bytes, as Ghidra decoded them */
} Probe;

/*
 * A field that could not be read is recorded AS SUCH, never as zeros.
 *
 * A deref of a null or unmapped pointer is a real thing for these functions to
 * do, and a recorder that quietly wrote 16 zero bytes would make two sides
 * that both failed to read look like two sides that agreed. The marker is
 * written into every byte of the field and counted; oraclediff.py reports the
 * count separately from the differences.
 */
#define PROBE_UNREADABLE 0xDB

/*
 * Stream layout, little-endian throughout (both sides are x86):
 *
 *   header   "X2PROBE1"  8 bytes
 *            u32 manifest_hash
 *            u32 side              PROBE_SIDE_*
 *            u32 nprobes           how many the writer knew about
 *            u32 reserved (0)
 *
 *   record   u32 probe_id
 *            u32 seq               per-probe call counter, from 0
 *            u32 nbytes            of `data`
 *            u8  data[nbytes]      every IN field in manifest order,
 *                                  then every OUT field in manifest order
 *
 * A record is written as ONE fwrite so a stream truncated by a kill ends at a
 * record boundary or is detectably short -- never half a record that reads as
 * a difference.
 */
typedef struct {
    FILE  *f;
    pr_u32 hash;
    int    side;
    pr_u64 records;
    pr_u64 unreadable;      /* fields that could not be read */
    pr_u64 dropped;         /* records lost because the buffer was too small */
} ProbeSink;

#define PROBE_MAX_RECORD 512

/*
 * Read `len` bytes of GUEST memory at `addr`, or return 0 without faulting.
 *
 * Each side answers only the per-PAGE question, over what it can check: the
 * port knows its own mappings (mincore), the stock side is inside the game's
 * address space with only Win32 to ask (VirtualQuery). The walk across pages
 * is HERE, once.
 *
 * It was written twice to begin with, and the second copy compared a
 * page-aligned end address against an unaligned start -- true for every
 * address not exactly on a page boundary, so every read on the stock side
 * failed and the capture was 24 unreadable fields out of 24. It was caught
 * only because an unreadable field is recorded and counted as such instead of
 * being written as zeros. One copy now, because the bug was in the duplication
 * rather than in either version of the arithmetic.
 */
int probe_page_readable(pr_u32 page);

#define PROBE_PAGE 4096u

static int probe_read(pr_u32 addr, void *dst, unsigned len)
{
    pr_u32 p, first, last;
    if (addr == 0 || len == 0) return 0;
    if (addr + len < addr) return 0;              /* wrapped the address space */
    first = addr & ~(PROBE_PAGE - 1u);
    last  = (addr + len - 1u) & ~(PROBE_PAGE - 1u);
    for (p = first; ; p += PROBE_PAGE) {
        if (!probe_page_readable(p)) return 0;
        if (p == last) break;
    }
    memcpy(dst, (const void *)(pr_uptr)addr, len);
    return 1;
}

static void pr_put32(unsigned char *p, pr_u32 v)
{
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

/*
 * Capture every field of phase `when` into `buf`.
 *
 * Returns the number of bytes written, or -1 if the manifest declares more
 * than `cap` bytes for this phase -- which the caller must report rather than
 * truncate, because a short record and a differing record are indistinguishable
 * once they are in the file.
 */
static int probe_capture(const Probe *p, int when, pr_u32 ecx, pr_u32 args,
                         unsigned char *buf, unsigned cap, pr_u64 *unreadable)
{
    unsigned n = 0;
    pr_u16 i;
    for (i = 0; i < p->nfields; i++) {
        const ProbeField *f = &p->fields[i];
        pr_u32 dword;
        if (f->when != when) continue;
        if (n + f->len > cap) return -1;
        dword = (f->src == PROBE_SRC_ECX) ? ecx : 0;
        if (f->src == PROBE_SRC_STACK && !probe_read(args + f->off, &dword, 4)) {
            memset(buf + n, PROBE_UNREADABLE, f->len);
            if (unreadable) (*unreadable)++;
            n += f->len;
            continue;
        }
        if (f->kind == PROBE_KIND_DWORD) {
            pr_put32(buf + n, dword);
        } else if (!probe_read(dword, buf + n, f->len)) {
            memset(buf + n, PROBE_UNREADABLE, f->len);
            if (unreadable) (*unreadable)++;
        }
        n += f->len;
    }
    return (int)n;
}

static int probe_sink_open(ProbeSink *s, const char *path, pr_u32 hash,
                           int side, pr_u32 nprobes)
{
    unsigned char hdr[PROBE_MAGIC_N + 16];
    memset(s, 0, sizeof *s);
    s->f = fopen(path, "wb");
    if (!s->f) return 0;
    s->hash = hash;
    s->side = side;
    memcpy(hdr, PROBE_MAGIC, PROBE_MAGIC_N);
    pr_put32(hdr + PROBE_MAGIC_N + 0, hash);
    pr_put32(hdr + PROBE_MAGIC_N + 4, (pr_u32)side);
    pr_put32(hdr + PROBE_MAGIC_N + 8, nprobes);
    pr_put32(hdr + PROBE_MAGIC_N + 12, 0);
    if (fwrite(hdr, 1, sizeof hdr, s->f) != sizeof hdr) return 0;
    /* Flushed immediately. Every run here is ended by a kill, and a stream
       whose header is still sitting in a stdio buffer is a zero-byte file --
       indistinguishable from a harness that never opened one. */
    fflush(s->f);
    return 1;
}

static void probe_emit(ProbeSink *s, const Probe *p, pr_u32 seq,
                       const unsigned char *in, int nin,
                       const unsigned char *out, int nout)
{
    unsigned char rec[12 + PROBE_MAX_RECORD];
    unsigned n;
    if (!s->f) return;
    if (nin < 0 || nout < 0 || (unsigned)(nin + nout) > PROBE_MAX_RECORD) {
        s->dropped++;
        return;
    }
    pr_put32(rec + 0, p->id);
    pr_put32(rec + 4, seq);
    pr_put32(rec + 8, (pr_u32)(nin + nout));
    memcpy(rec + 12, in, (unsigned)nin);
    memcpy(rec + 12 + nin, out, (unsigned)nout);
    n = 12u + (unsigned)(nin + nout);
    if (fwrite(rec, 1, n, s->f) == n) s->records++;
    else s->dropped++;
}

/*
 * The closing report, printed by BOTH sides in the same words.
 *
 * It prints at zero. A run in which no probe ever fired is the most important
 * thing this harness can tell you -- it means the hooks did not take, and
 * every "no differences found" downstream would otherwise be a lie.
 */
static void probe_sink_close(ProbeSink *s, const char *who)
{
    fprintf(stderr,
            "probe[%s]: %llu record(s), %llu unreadable field(s), "
            "%llu dropped\n",
            who, (unsigned long long)s->records,
            (unsigned long long)s->unreadable,
            (unsigned long long)s->dropped);
    if (s->records == 0)
        fprintf(stderr,
                "probe[%s]: ZERO records. No probed function was ever called, "
                "which means the hooks did not take -- not that the two sides "
                "agree. Do not compare this stream.\n", who);
    if (s->f) { fclose(s->f); s->f = NULL; }
}

#endif /* PROBE_REC_H */
