/*
 * MSVCR71 -- the C runtime XMen2.exe imports, on libc.
 *
 * The DLLs import MSVCRT.dll and the exe imports MSVCR71.dll: different DLL
 * names, same semantics, so the implementations live here once and the two
 * name-spellings are aliased at the bottom of the file rather than duplicated.
 *
 * 87 symbols are imported. This file implements the ones whose behaviour is
 * unambiguous; the rest still abort by name, and the two groups are worth
 * distinguishing:
 *
 *   - C++ exception handling (__CxxFrameHandler, _CxxThrowException,
 *     _except_handler3, _XcptFilter, ?terminate@@YAXXZ, the exception class
 *     itself) is NOT implemented. It needs a real SEH unwinder walking the
 *     FS:[0] chain, which this build maintains but does not walk. Faking any
 *     of it would turn a throw into silent corruption.
 *   - the varargs family (printf, sprintf, sscanf, _vsnprintf, vsprintf) is
 *     not implemented either: a va_list cannot be synthesised portably from
 *     the guest stack, so it needs a small format walker rather than a
 *     forward to libc.
 *
 * Conventions, all measured against the emitted call sites:
 *   - the C runtime is __cdecl: the CALLER pops, so ESP moves by 4 (the
 *     return address) and nothing else.
 *   - a double is TWO stack dwords, and a double RESULT comes back in ST(0)
 *     of the modelled x87 stack, not in EAX.
 *   - the _CI* entry points take their arguments already ON the x87 stack.
 *   - guest pointers are host pointers under the current identity mapping, so
 *     a `const char *` argument can be used directly. Allocation cannot: it
 *     has to come from the guest heap (C083).
 */
#include "x86rt.h"
#include "guest_heap.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#define A(i)  RD32(C->esp + 4u + (uint32_t)(i) * 4u)
#define AP(i) ((void *)(uintptr_t)A(i))
#define AS(i) ((char *)(uintptr_t)A(i))
#define ACS(i) ((const char *)(uintptr_t)A(i))

static void ret_c(CPU *C, uint32_t eax) { C->eax = eax; C->esp += 4u; }

/* A double argument occupies two dwords, little-endian. */
static double argd(CPU *C, int i)
{
    uint64_t lo = A(i), hi = A(i + 1);
    uint64_t bits = lo | (hi << 32);
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

/* Push a result onto the modelled x87 stack, where MSVC expects a double
   return value to be. Overflowing it is a translation bug, not a runtime
   condition, so it stops. */
static void crt_x87_push(CPU *C, long double v)
{
    if (C->depth >= 8) x87_fault("x87 stack overflow returning from the CRT");
    C->top = (C->top - 1) & 7;
    C->st[C->top] = v;
    C->depth++;
}

static long double crt_x87_pop(CPU *C)
{
    long double v;
    if (C->depth < 1) x87_fault("x87 stack underflow entering a _CI* routine");
    v = C->st[C->top];
    C->top = (C->top + 1) & 7;
    C->depth--;
    return v;
}

static void ret_d(CPU *C, double v) { crt_x87_push(C, (long double)v); C->esp += 4u; }

static void crt_unimpl(const char *sym, const char *why)
{
    fprintf(stderr, "crt: MSVCR71!%s is not implemented natively.\n  %s\n",
            sym, why);
    abort();
}

/* ---- memory ------------------------------------------------------------ */

void imp_MSVCR71_malloc(CPU *C)  { ret_c(C, guest_malloc(A(0))); }
void imp_MSVCR71_free(CPU *C)    { guest_free(A(0)); ret_c(C, 0); }
void imp_MSVCR71_realloc(CPU *C) { ret_c(C, guest_realloc(A(0), A(1))); }

/*
 * C++ operator new / delete / delete[].
 *
 * The mangled names become C identifiers by replacing every non-word character
 * with '_', which is why these read as they do:
 *     ??2@YAPAXI@Z   operator new(unsigned int)     -> __2_YAPAXI_Z
 * (delete and delete[] were already here; only new was missing.)
 *
 * They are malloc and free on the guest heap, which is the whole of it: the
 * engine overrides operator new for its own pools where it wants to (igMemory
 * has its own), and the global one is the plain allocator underneath. A NULL
 * return is honest here -- the C++ standard would throw std::bad_alloc, and
 * this build has no working throw, so a caller that does not check gets a NULL
 * dereference at the point of use rather than silent corruption.
 */
void imp_MSVCR71___2_YAPAXI_Z(CPU *C) { ret_c(C, guest_malloc(A(0))); }

void imp_MSVCR71_calloc(CPU *C)
{
    uint32_t n = A(0) * A(1), p = guest_malloc(n);
    if (p) memset((void *)(uintptr_t)p, 0, n);
    ret_c(C, p);
}

/* operator new / operator delete / vector delete. new must return zero on
   failure rather than throwing: the throwing form goes through _callnewh. */
void imp_MSVCR71___3_YAXPAX_Z(CPU *C)  { guest_free(A(0)); ret_c(C, 0); }
void imp_MSVCR71___V_YAXPAX_Z(CPU *C)  { guest_free(A(0)); ret_c(C, 0); }

void imp_MSVCR71__callnewh(CPU *C)
{
    /* The new-handler hook. There is none installed, and returning 0 says
       exactly that: "no handler retried the allocation". */
    ret_c(C, 0);
}

void imp_MSVCR71_memmove(CPU *C)
{
    memmove(AP(0), AP(1), A(2));
    ret_c(C, A(0));
}

/* ---- string and ctype -------------------------------------------------- */

void imp_MSVCR71_strchr(CPU *C)   { ret_c(C, (uint32_t)(uintptr_t)strchr(ACS(0), (int)A(1))); }
void imp_MSVCR71_strrchr(CPU *C)  { ret_c(C, (uint32_t)(uintptr_t)strrchr(ACS(0), (int)A(1))); }
void imp_MSVCR71_strstr(CPU *C)   { ret_c(C, (uint32_t)(uintptr_t)strstr(ACS(0), ACS(1))); }
void imp_MSVCR71_strtok(CPU *C)   { ret_c(C, (uint32_t)(uintptr_t)strtok(A(0) ? AS(0) : NULL, ACS(1))); }
void imp_MSVCR71_strcspn(CPU *C)  { ret_c(C, (uint32_t)strcspn(ACS(0), ACS(1))); }
void imp_MSVCR71_strncat(CPU *C)  { strncat(AS(0), ACS(1), A(2)); ret_c(C, A(0)); }
void imp_MSVCR71_strncpy(CPU *C)  { strncpy(AS(0), ACS(1), A(2)); ret_c(C, A(0)); }
void imp_MSVCR71_strncmp(CPU *C)  { ret_c(C, (uint32_t)strncmp(ACS(0), ACS(1), A(2))); }
void imp_MSVCR71__strcmpi(CPU *C) { ret_c(C, (uint32_t)strcasecmp(ACS(0), ACS(1))); }
void imp_MSVCR71__stricmp(CPU *C) { ret_c(C, (uint32_t)strcasecmp(ACS(0), ACS(1))); }
void imp_MSVCR71__strnicmp(CPU *C){ ret_c(C, (uint32_t)strncasecmp(ACS(0), ACS(1), A(2))); }

void imp_MSVCR71__strlwr(CPU *C)
{
    char *s = AS(0), *p;
    for (p = s; *p; p++) *p = (char)tolower((unsigned char)*p);
    ret_c(C, A(0));
}

void imp_MSVCR71__strupr(CPU *C)
{
    char *s = AS(0), *p;
    for (p = s; *p; p++) *p = (char)toupper((unsigned char)*p);
    ret_c(C, A(0));
}

void imp_MSVCR71__strnset(CPU *C)
{
    char *s = AS(0);
    uint32_t n = A(2), i;
    for (i = 0; i < n && s[i]; i++) s[i] = (char)A(1);
    ret_c(C, A(0));
}

void imp_MSVCR71_isalnum(CPU *C) { ret_c(C, (uint32_t)isalnum((int)A(0))); }
void imp_MSVCR71_isdigit(CPU *C) { ret_c(C, (uint32_t)isdigit((int)A(0))); }
void imp_MSVCR71_islower(CPU *C) { ret_c(C, (uint32_t)islower((int)A(0))); }
void imp_MSVCR71_ispunct(CPU *C) { ret_c(C, (uint32_t)ispunct((int)A(0))); }
void imp_MSVCR71_isspace(CPU *C) { ret_c(C, (uint32_t)isspace((int)A(0))); }
void imp_MSVCR71_tolower(CPU *C) { ret_c(C, (uint32_t)tolower((int)A(0))); }
void imp_MSVCR71_toupper(CPU *C) { ret_c(C, (uint32_t)toupper((int)A(0))); }

void imp_MSVCR71__ismbblead(CPU *C)
{
    /* Multi-byte lead byte, for double-byte codepages. This build is
       single-byte throughout (MultiByteToWideChar refuses anything else), so
       "never a lead byte" is the consistent answer rather than a guess. */
    ret_c(C, 0);
}

void imp_MSVCR71__itoa(CPU *C)
{
    char *buf = AS(1);
    int radix = (int)A(2);
    if (radix == 10) snprintf(buf, 32, "%d", (int)A(0));
    else if (radix == 16) snprintf(buf, 32, "%x", A(0));
    else if (radix == 8) snprintf(buf, 32, "%o", A(0));
    else crt_unimpl("_itoa", "only radix 8, 10 and 16 are implemented");
    ret_c(C, A(1));
}

/* ---- numbers ----------------------------------------------------------- */

void imp_MSVCR71_atoi(CPU *C) { ret_c(C, (uint32_t)atoi(ACS(0))); }
void imp_MSVCR71_atof(CPU *C) { ret_d(C, atof(ACS(0))); }
void imp_MSVCR71_ceil(CPU *C) { ret_d(C, ceil(argd(C, 0))); }
void imp_MSVCR71_floor(CPU *C) { ret_d(C, floor(argd(C, 0))); }

void imp_MSVCR71__finite(CPU *C) { ret_c(C, (uint32_t)(isfinite(argd(C, 0)) ? 1 : 0)); }

/* The _CI* forms take their arguments on the x87 stack and leave the result
   there. Order matters: the SECOND operand is on top. */
void imp_MSVCR71__CIpow(CPU *C)
{
    long double y = crt_x87_pop(C), x = crt_x87_pop(C);
    crt_x87_push(C, powl(x, y));
    C->esp += 4u;
}

void imp_MSVCR71__CIfmod(CPU *C)
{
    long double y = crt_x87_pop(C), x = crt_x87_pop(C);
    crt_x87_push(C, fmodl(x, y));
    C->esp += 4u;
}

void imp_MSVCR71__CIacos(CPU *C)
{
    long double x = crt_x87_pop(C);
    crt_x87_push(C, acosl(x));
    C->esp += 4u;
}

void imp_MSVCR71_rand(CPU *C) { ret_c(C, (uint32_t)(rand() & 0x7FFF)); }
void imp_MSVCR71_srand(CPU *C) { srand(A(0)); ret_c(C, 0); }
void imp_MSVCR71_clock(CPU *C) { ret_c(C, (uint32_t)(clock() / (CLOCKS_PER_SEC / 1000))); }
void imp_MSVCR71_time(CPU *C)
{
    time_t t = time(NULL);
    if (A(0)) WR32(A(0), (uint32_t)t);
    ret_c(C, (uint32_t)t);
}

/* ---- stdio -------------------------------------------------------------
 *
 * A FILE * does not fit in a guest pointer on x86-64, so the guest gets a
 * small handle and this side keeps the table. Handles start at 1 so that 0
 * stays "failed", which is what the caller tests.
 */
/* Defined further down (the format walker) and in kernel32.c (path
   translation); declared here so stdio can use both. */
int guest_vformat(char *out, size_t cap, const char *fmt, uint32_t va);
const char *win_path(const char *in);

#define MAX_FILES 64
static FILE *g_files[MAX_FILES];

/*
 * _iob -- the guest's stdin/stdout/stderr.
 *
 * MSVCRT exports it as DATA, and the guest reaches a standard stream by taking
 * the address of an element: stderr is &_iob[2]. So a FILE* here is EITHER one
 * of our small handles from fopen, or a pointer into that array, and fh() has
 * to accept both. They cannot be confused: a handle is 1..64 and the array
 * lives on the guest heap.
 *
 * MSVC's FILE (struct _iobuf) is 32 bytes. Nothing here reads its fields -- the
 * array exists so the ADDRESS arithmetic works and the pointer can be
 * recognised -- so it is zeroed rather than filled with a fake buffer.
 */
#define IOB_N 3
#define IOB_SIZEOF_FILE 32u
static uint32_t g_iob;                 /* guest address of _iob[0] */

uint32_t crt_iob_base(void)
{
    if (!g_iob) {
        g_iob = guest_malloc(IOB_N * IOB_SIZEOF_FILE);
        if (!g_iob) {
            fprintf(stderr, "crt: could not allocate _iob on the guest heap\n");
            abort();
        }
        memset((void *)(uintptr_t)g_iob, 0, IOB_N * IOB_SIZEOF_FILE);
    }
    return g_iob;
}

static FILE *fh(uint32_t h)
{
    if (g_iob && h >= g_iob && h < g_iob + IOB_N * IOB_SIZEOF_FILE) {
        uint32_t i = (h - g_iob) / IOB_SIZEOF_FILE;
        /* stdin is not readable in this host -- it is not connected to
           anything -- so a read from it must not silently return EOF as if the
           stream were merely empty. */
        return i == 0 ? stdin : i == 1 ? stdout : stderr;
    }
    if (h == 0 || h > MAX_FILES || !g_files[h - 1]) {
        fprintf(stderr, "crt: file handle %u is not open, and it is not a "
                        "pointer into _iob either\n", h);
        abort();
    }
    return g_files[h - 1];
}

void imp_MSVCR71_fopen(CPU *C)
{
    int i;
    for (i = 0; i < MAX_FILES; i++) {
        if (g_files[i]) continue;
        /* Through win_path, like every other open in this host: the game
           ships Windows paths whose case does not match the extracted files. */
        g_files[i] = fopen(win_path(ACS(0)), ACS(1));
        ret_c(C, g_files[i] ? (uint32_t)(i + 1) : 0u);
        return;
    }
    fprintf(stderr, "crt: more than %d files open at once\n", MAX_FILES);
    abort();
}

void imp_MSVCR71_fclose(CPU *C)
{
    uint32_t h = A(0);
    int rc = fclose(fh(h));
    g_files[h - 1] = NULL;
    ret_c(C, (uint32_t)rc);
}

void imp_MSVCR71_fread(CPU *C)
{
    ret_c(C, (uint32_t)fread(AP(0), A(1), A(2), fh(A(3))));
}

void imp_MSVCR71_fseek(CPU *C)
{
    ret_c(C, (uint32_t)fseek(fh(A(0)), (long)(int32_t)A(1), (int)A(2)));
}

void imp_MSVCR71_ftell(CPU *C) { ret_c(C, (uint32_t)ftell(fh(A(0)))); }

void imp_MSVCR71__mkdir(CPU *C) { ret_c(C, (uint32_t)mkdir(win_path(ACS(0)), 0777)); }

/* ---- the rest of stdio -------------------------------------------------- */

void imp_MSVCR71_fflush(CPU *C)  { ret_c(C, (uint32_t)fflush(A(0) ? fh(A(0)) : NULL)); }
void imp_MSVCR71_fputc(CPU *C)   { ret_c(C, (uint32_t)fputc((int)A(0), fh(A(1)))); }
void imp_MSVCR71_fputs(CPU *C)   { ret_c(C, (uint32_t)fputs(ACS(0), fh(A(1)))); }
void imp_MSVCR71_fgetc(CPU *C)   { ret_c(C, (uint32_t)fgetc(fh(A(0)))); }
void imp_MSVCR71_ungetc(CPU *C)  { ret_c(C, (uint32_t)ungetc((int)A(0), fh(A(1)))); }
void imp_MSVCR71_fwrite(CPU *C)
{
    ret_c(C, (uint32_t)fwrite(AP(0), A(1), A(2), fh(A(3))));
}
void imp_MSVCR71_fgets(CPU *C)
{
    char *r = fgets(AS(0), (int)A(1), fh(A(2)));
    ret_c(C, r ? A(0) : 0u);
}
void imp_MSVCR71_fprintf(CPU *C)
{
    char buf[4096];
    int n = guest_vformat(buf, sizeof buf, ACS(1), C->esp + 4u + 2u * 4u);
    if (n >= 0) fputs(buf, fh(A(0)));
    ret_c(C, (uint32_t)n);
}
void imp_MSVCR71_vfprintf(CPU *C)
{
    char buf[4096];
    int n = guest_vformat(buf, sizeof buf, ACS(1), A(2));
    if (n >= 0) fputs(buf, fh(A(0)));
    ret_c(C, (uint32_t)n);
}

/* ---- startup and exit -------------------------------------------------- */

void imp_MSVCR71__controlfp(CPU *C)
{
    /* The x87 control word is modelled in the CPU struct, and FLDCW/FNSTCW go
       through it. Reporting the modelled value keeps the two consistent
       instead of answering from the host FPU, which the guest never uses. */
    uint32_t newv = A(0), mask = A(1);
    C->fcw = (C->fcw & ~mask) | (newv & mask);
    ret_c(C, C->fcw);
}

void imp_MSVCR71___set_app_type(CPU *C) { ret_c(C, 0); }        /* console/GUI hint */
void imp_MSVCR71___setusermatherr(CPU *C) { ret_c(C, 0); }      /* no matherr hook */

void imp_MSVCR71__exit(CPU *C)  { exit((int)A(0)); }
void imp_MSVCR71_exit(CPU *C)   { exit((int)A(0)); }
void imp_MSVCR71__c_exit(CPU *C){ ret_c(C, 0); }
void imp_MSVCR71__cexit(CPU *C) { ret_c(C, 0); }

void imp_MSVCR71__amsg_exit(CPU *C)
{
    fprintf(stderr, "crt: the guest CRT called _amsg_exit(%u) -- a fatal "
                    "runtime error inside the recompiled program\n", A(0));
    exit(3);
}

void imp_MSVCR71__purecall(CPU *C)
{
    fprintf(stderr, "crt: pure virtual function called -- an object was used "
                    "before its constructor ran, or after its destructor\n");
    abort();
}

void imp_MSVCR71_qsort(CPU *C)
{
    /* The comparator is GUEST code, so this cannot forward to libc qsort: the
       callback has to go back through the recompiler. An insertion sort keeps
       the reentrancy trivial; these tables are small and it can be replaced
       when a measurement says it matters. */
    uint32_t base = A(0), n = A(1), sz = A(2), cmp = A(3), i, j;
    unsigned char *tmp = malloc(sz);
    if (!tmp) { ret_c(C, 0); return; }
    for (i = 1; i < n; i++) {
        memcpy(tmp, (void *)(uintptr_t)(base + i * sz), sz);
        for (j = i; j > 0; j--) {
            uint32_t prev = base + (j - 1) * sz;
            CPU K = *C;
            K.esp -= 8u;
            WR32(K.esp + 0u, prev);
            WR32(K.esp + 4u, (uint32_t)(uintptr_t)tmp);
            x86_guest_call(&K, cmp);
            if ((int32_t)K.eax <= 0) break;
            memmove((void *)(uintptr_t)(base + j * sz),
                    (void *)(uintptr_t)prev, sz);
        }
        memcpy((void *)(uintptr_t)(base + j * sz), tmp, sz);
    }
    free(tmp);
    ret_c(C, 0);
}

/* ---- deliberately not implemented -------------------------------------- */


/* ---- the format walker -------------------------------------------------
 *
 * A va_list on x86-32 cdecl IS a pointer into the guest stack: arguments are
 * pushed right-to-left, each padded to 4 bytes, doubles taking 8. So the
 * varargs family does not need a synthesised va_list at all -- it needs this,
 * a walk of the format string that pulls each argument from guest memory by
 * hand and formats it one directive at a time with the host's snprintf.
 *
 * Everything it cannot handle STOPS by name. That matters more here than
 * usual: the alternative to refusing an unknown conversion is emitting
 * something plausible into a string the game then uses as a path, a key or a
 * displayed line, and the damage would surface far away from the cause.
 */
static uint32_t g_vfmt_va;                 /* the walking guest va pointer */

static uint32_t va_dword(void)  { uint32_t v = RD32(g_vfmt_va); g_vfmt_va += 4u; return v; }
static uint64_t va_qword(void)
{
    uint64_t lo = RD32(g_vfmt_va), hi = RD32(g_vfmt_va + 4u);
    g_vfmt_va += 8u;
    return lo | (hi << 32);
}
static double va_double(void)
{
    union { uint64_t u; double d; } u;
    u.u = va_qword();
    return u.d;
}

/*
 * Returns the length that WOULD have been written (snprintf semantics), and
 * writes at most cap bytes including the NUL. MSVC's _snprintf family differs
 * from C99 -- it returns -1 on truncation and does not always NUL-terminate --
 * so the callers below adjust rather than this doing it two ways.
 */
int guest_vformat(char *out, size_t cap, const char *fmt, uint32_t va)
{
    size_t used = 0;
    char spec[64], tmp[512];
    const char *p = fmt;
    g_vfmt_va = va;
    if (!fmt) {
        crt_unimpl("_vsnprintf", "the format string pointer is NULL");
        return -1;
    }
    while (*p) {
        int n = 0, si = 0, star_w = 0, star_p = 0, lng = 0;
        if (*p != '%') {
            if (used + 1 < cap) out[used] = *p;
            used++; p++;
            continue;
        }
        spec[si++] = *p++;                                  /* '%' */
        if (*p == '%') { if (used + 1 < cap) out[used] = '%'; used++; p++; continue; }
        while (*p && strchr("-+ #0", *p) && si < 40) spec[si++] = *p++;   /* flags */
        if (*p == '*') { star_w = 1; p++; }
        else while (*p >= '0' && *p <= '9' && si < 40) spec[si++] = *p++; /* width */
        if (*p == '.') {
            spec[si++] = *p++;
            if (*p == '*') { star_p = 1; p++; }
            else while (*p >= '0' && *p <= '9' && si < 50) spec[si++] = *p++;
        }
        /* length modifiers; MSVC also spells 64-bit as I64 */
        if (p[0] == 'I' && p[1] == '6' && p[2] == '4') { lng = 2; p += 3; }
        else if (p[0] == 'l' && p[1] == 'l') { lng = 2; p += 2; }
        else if (*p == 'l' || *p == 'L') { lng = (*p == 'L') ? 3 : 1; p++; }
        else if (*p == 'h') { lng = -1; p++; }
        if (star_w) {
            int w = (int)va_dword();
            si += snprintf(spec + si, sizeof spec - si, "%d", w);
        }
        if (star_p) {
            int pr = (int)va_dword();
            si += snprintf(spec + si, sizeof spec - si, ".%d", pr);
        }
        switch (*p) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o':
            if (lng == 2) {
                spec[si++] = 'l'; spec[si++] = 'l'; spec[si++] = *p; spec[si] = 0;
                n = snprintf(tmp, sizeof tmp, spec, (long long)va_qword());
            } else {
                spec[si++] = *p; spec[si] = 0;
                n = snprintf(tmp, sizeof tmp, spec, (int)va_dword());
            }
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
            spec[si++] = *p; spec[si] = 0;
            n = snprintf(tmp, sizeof tmp, spec, va_double());
            break;
        case 'c':
            spec[si++] = 'c'; spec[si] = 0;
            n = snprintf(tmp, sizeof tmp, spec, (int)(va_dword() & 0xFF));
            break;
        case 'p':
            spec[si++] = 'p'; spec[si] = 0;
            n = snprintf(tmp, sizeof tmp, spec, (void *)(uintptr_t)va_dword());
            break;
        case 's': {
            uint32_t sp = va_dword();
            spec[si++] = 's'; spec[si] = 0;
            /* A NULL string is printed as MSVC prints it rather than crashing
               the host on a guest bug. */
            n = snprintf(tmp, sizeof tmp, spec,
                         sp ? (const char *)(uintptr_t)sp : "(null)");
            break;
        }
        default:
            /* Refuse: see the header comment. The conversion is named so the
               next one can be added deliberately. */
            fprintf(stderr, "crt: format walker met %%%c, which it does not "
                            "implement -- refusing rather than inventing "
                            "output for it\n", *p ? *p : '?');
            crt_unimpl("_vsnprintf", "unimplemented printf conversion");
            return -1;
        }
        p++;
        if (n < 0) return -1;
        {   int i;
            for (i = 0; i < n; i++) {
                if (used + 1 < cap) out[used] = tmp[i];
                used++;
            }
        }
    }
    if (cap) out[used < cap ? used : cap - 1] = 0;
    return (int)used;
}

/* MSVC's _snprintf/_vsnprintf: return -1 when the result did not fit, and do
   not NUL-terminate in that case. C99's snprintf differs on both counts, so
   the difference is applied here rather than left as a subtle mismatch. */
static int msvc_trunc(int want, uint32_t buf, uint32_t count)
{
    if (want < 0) return -1;
    if ((uint32_t)want >= count) return -1;
    (void)buf;
    return want;
}

#define NOT_IMPL(name, why) \
    void imp_MSVCR71_##name(CPU *C) { (void)C; crt_unimpl(#name, why); }

#define EH_WHY "C++ exception handling needs a real SEH unwinder walking the " \
               "FS:[0] chain. This build keeps that chain well-formed but does " \
               "not walk it, and faking a throw would be silent corruption."
#define VA_WHY "the varargs family needs a format walker over the guest stack; " \
               "a va_list cannot be synthesised portably from it."

NOT_IMPL(__CxxFrameHandler, EH_WHY)
NOT_IMPL(_CxxThrowException, EH_WHY)
NOT_IMPL(_except_handler3, EH_WHY)
NOT_IMPL(_XcptFilter, EH_WHY)
NOT_IMPL(__RTDynamicCast, EH_WHY)
NOT_IMPL(_setjmp3, "setjmp/longjmp across recompiled frames needs the guest "
                   "register file saved, not the host's")
NOT_IMPL(longjmp, "see _setjmp3")

/* ---- shared with the DLLs' MSVCRT --------------------------------------
 *
 * The exe imports MSVCR71.dll and the DLLs import MSVCRT.dll. Same functions,
 * different spelling of the containing module, and the recompiler names an
 * import stub after both. Aliasing rather than reimplementing: two copies of
 * _initterm would be two things to keep in step, and the one that drifts is
 * the one nobody is looking at.
 */
/* ---- moved from win32_sdl.c: the CRT pieces the DLLs reach ------------- */

const char *x86_native_name_at(uint32_t addr);
/* Report a constructor target as its module and GUEST address: the modules are
   relocated now, and Ghidra seeds have to be given the address the module was
   LINKED for, not where it happens to be mapped. Printing the mapped address
   produced seeds that pointed nowhere. */
void x86_guest_addr_of(uint32_t addr, const char **mod, uint32_t *guest);

void imp_MSVCR71__initterm(CPU *C)
{
    /* Walk the function-pointer table and call each non-NULL entry through the
       recompiled dispatcher -- these are the module's static constructors, and
       skipping them would leave every global unconstructed.
     *
     * The pre-pass is the point. These tables are the ONLY reference to most
     * of their targets: they are data pointers in .rdata, so static analysis
     * never marked them as code and they have no recompiled body. Running the
     * table straight through would stop at the first one, and each rebuild
     * would reveal exactly one more. Listing every missing target first turns
     * that into one seed list.
     */
    uint32_t p = A(0), end = A(1), n = 0, missing = 0;
    for (p = A(0); p < end; p += 4u) {
        uint32_t fn = RD32(p);
        if (!fn) continue;
        n++;
        if (!x86_native_name_at(fn)) {
            if (!missing)
                fprintf(stderr, "\n*** _initterm: constructor targets with no "
                                "recompiled body.\n    These are reachable only "
                                "through this table, so static analysis never "
                                "saw them as code.\n    Seed them and re-lift; "
                                "the full list follows so this costs one pass, "
                                "not one per rebuild.\n");
            {
                const char *mn = NULL; uint32_t g = fn;
                x86_guest_addr_of(fn, &mn, &g);
                fprintf(stderr, "    %-18s 0x%08x\n", mn ? mn : "(no module)", g);
            }
            missing++;
        }
    }
    if (missing) {
        fprintf(stderr, "*** %u of %u constructor targets are missing bodies\n",
                missing, n);
        abort();
    }
    for (p = A(0); p < end; p += 4u) {
        uint32_t fn = RD32(p);
        if (fn) x86_guest_call(C, fn);
    }
    ret_c(C, 0);
}


/*
 * __getmainargs(&argc, &argv, &envp, doWildCard, startupInfo)
 *
 * The CRT calls this to build argc/argv before main. A real command line is
 * not available -- and inventing one would be a lie the game could branch on,
 * since it parses its own arguments -- so it gets exactly the program name,
 * which is what argv[0] is for and what running with no arguments looks like.
 */
void imp_MSVCR71___getmainargs(CPU *C)
{
    static uint32_t argv_block;
    if (!argv_block) {
        uint32_t strp = guest_malloc(16);
        argv_block = guest_malloc(8);
        memcpy((void *)(uintptr_t)strp, "XMen2.exe", 10);
        WR32(argv_block, strp);
        WR32(argv_block + 4u, 0);
    }
    WR32(A(0), 1);                      /* argc */
    WR32(A(1), argv_block);             /* argv */
    WR32(A(2), argv_block + 4u);        /* envp: the NUL terminator, i.e. empty */
    ret_c(C, 0);
}

/* _onexit(func): register an atexit handler.
 *
 * Grows rather than capping. The first version stopped at 64 on the grounds
 * that a fixed limit is honest -- and it is, but only if the limit is a real
 * property of the thing. This one was a number I picked, and the game sailed
 * past it during startup. A cap that exists only because someone guessed is
 * not a constraint, it is a bug with an error message. */
static uint32_t *g_onexit;
static int g_nonexit, g_onexit_cap;

void imp_MSVCR71__onexit(CPU *C)
{
    if (g_nonexit == g_onexit_cap) {
        int cap = g_onexit_cap ? g_onexit_cap * 2 : 64;
        uint32_t *nt = realloc(g_onexit, (size_t)cap * sizeof *nt);
        if (!nt) { ret_c(C, 0); return; }
        g_onexit = nt;
        g_onexit_cap = cap;
    }
    g_onexit[g_nonexit++] = A(0);
    ret_c(C, A(0));
}

/* __p__commode / __p__fmode return POINTERS to the CRT's file-mode globals, so
   they need real, writable guest words rather than values. */
static uint32_t mode_word(uint32_t *slot, uint32_t initial)
{
    if (!*slot) { *slot = guest_malloc(4); WR32(*slot, initial); }
    return *slot;
}

void imp_MSVCR71___p__commode(CPU *C)
{
    static uint32_t w;
    ret_c(C, mode_word(&w, 0));          /* _IOCOMMIT off, the CRT default */
}

void imp_MSVCR71___p__fmode(CPU *C)
{
    static uint32_t w;
    ret_c(C, mode_word(&w, 0));          /* _O_TEXT, the CRT default */
}

void imp_MSVCR71___security_error_handler(CPU *C)
{
    fprintf(stderr, "crt: the guest's stack-check handler fired -- a buffer "
                    "overrun was detected inside recompiled code\n");
    abort();
}

/* ---- memory and string primitives -------------------------------------- */

void imp_MSVCR71_memcpy(CPU *C)  { memcpy(AP(0), AP(1), A(2)); ret_c(C, A(0)); }
void imp_MSVCR71_memset(CPU *C)  { memset(AP(0), (int)A(1), A(2)); ret_c(C, A(0)); }
void imp_MSVCR71_strlen(CPU *C)  { ret_c(C, (uint32_t)strlen(ACS(0))); }
void imp_MSVCR71_strcpy(CPU *C)  { strcpy(AS(0), ACS(1)); ret_c(C, A(0)); }
void imp_MSVCR71_strcat(CPU *C)  { strcat(AS(0), ACS(1)); ret_c(C, A(0)); }
void imp_MSVCR71_strcmp(CPU *C)  { ret_c(C, (uint32_t)strcmp(ACS(0), ACS(1))); }
void imp_MSVCR71_isalpha(CPU *C) { ret_c(C, (uint32_t)isalpha((int)A(0))); }

void imp_MSVCR71__strdup(CPU *C)
{
    /* Guest heap, not strdup(): the result is a guest pointer. */
    const char *s = ACS(0);
    uint32_t n = (uint32_t)strlen(s) + 1u, p = guest_malloc(n);
    if (p) memcpy((void *)(uintptr_t)p, s, n);
    ret_c(C, p);
}

/* ---- math -------------------------------------------------------------- */

void imp_MSVCR71_atan2(CPU *C) { ret_d(C, atan2(argd(C, 0), argd(C, 2))); }
void imp_MSVCR71_exp(CPU *C)   { ret_d(C, exp(argd(C, 0))); }
void imp_MSVCR71_log(CPU *C)   { ret_d(C, log(argd(C, 0))); }
void imp_MSVCR71_pow(CPU *C)   { ret_d(C, pow(argd(C, 0), argd(C, 2))); }
void imp_MSVCR71_sqrt(CPU *C)  { ret_d(C, sqrt(argd(C, 0))); }
void imp_MSVCR71_fabs(CPU *C)  { ret_d(C, fabs(argd(C, 0))); }
void imp_MSVCR71_strtod(CPU *C)
{
    char *end = NULL;
    double v = strtod(ACS(0), &end);
    if (A(1)) WR32(A(1), (uint32_t)(uintptr_t)end);
    ret_d(C, v);
}

void imp_MSVCR71__CIasin(CPU *C)
{
    long double x = crt_x87_pop(C);
    crt_x87_push(C, asinl(x));
    C->esp += 4u;
}

/* ---- misc -------------------------------------------------------------- */

void imp_MSVCR71_abort(CPU *C)
{
    (void)C;
    fprintf(stderr, "crt: the recompiled program called abort()\n");
    abort();
}

void imp_MSVCR71__errno(CPU *C)
{
    /* Returns a POINTER to errno, so it needs a real guest word. One shared
       cell: this build is single-threaded, and errno is per-thread only
       because threads exist. */
    static uint32_t cell;
    if (!cell) { cell = guest_malloc(4); if (cell) WR32(cell, 0); }
    ret_c(C, cell);
}

void imp_MSVCR71_getenv(CPU *C)
{
    /* The host environment, copied into guest memory so the pointer fits. The
       copies are never freed, which is correct: getenv's result is meant to
       stay valid for the life of the program. */
    const char *v = getenv(ACS(0));
    uint32_t p;
    if (!v) { ret_c(C, 0); return; }
    p = guest_malloc((uint32_t)strlen(v) + 1u);
    if (p) memcpy((void *)(uintptr_t)p, v, strlen(v) + 1);
    ret_c(C, p);
}

void imp_MSVCR71_setlocale(CPU *C)
{
    /* Only the C locale exists here, and returning its name is the truth
       rather than a placeholder -- the game checks the result for NULL. */
    static uint32_t name;
    if (!name) {
        name = guest_malloc(2);
        if (name) { WR8(name, 'C'); WR8(name + 1u, 0); }
    }
    ret_c(C, name);
}

/*
 * ---- one implementation, two module names ------------------------------
 *
 * The DLLs import MSVCRT.dll and the exe imports MSVCR71.dll. Same runtime,
 * two spellings, and the recompiler names a stub after each. Aliasing rather
 * than reimplementing: two copies of anything here would be two things to keep
 * in step, and the one that drifts is the one nobody is looking at.
 *
 * Only functions that are actually implemented above appear here. An MSVCRT
 * import with no entry keeps its generated stub and stops by name, which is
 * the behaviour that has been finding these one honest step at a time.
 */

/* __ftol: the argument arrives on the x87 stack and the truncated 64-bit
   result goes back in EDX:EAX. It pops one register, which the lazy x87 model
   tracks in `depth`. */
void imp_MSVCR71__ftol(CPU *C)
{
    long double v = crt_x87_pop(C);
    int64_t r = (int64_t)v;
    C->eax = (uint32_t)(uint64_t)r;
    C->edx = (uint32_t)((uint64_t)r >> 32);
    C->esp += 4u;                        /* __cdecl, no stack arguments */
}

/*
 * __dllonexit(func, pbegin, pend) -- register a static destructor.
 *
 * Implemented rather than stubbed even though nothing runs the destructors
 * yet: a stub returning `func` looks identical from the caller's side while
 * dropping every registration, and the table it maintains is guest-visible.
 * Growing by one entry per call is not how MSVCRT does it (it doubles), but
 * the observable state -- *pbegin, *pend, and the entries between -- matches.
 */
void imp_MSVCR71___dllonexit(CPU *C)
{
    uint32_t func = A(0), pbegin = A(1), pend = A(2);
    uint32_t b = RD32(pbegin), e = RD32(pend);
    uint32_t count = b ? (e - b) / 4u : 0u;
    uint32_t nt;

    if (!func) { ret_c(C, 0); return; }
    nt = guest_realloc(b, (count + 1u) * 4u);
    if (!nt) { ret_c(C, 0); return; }
    WR32(nt + count * 4u, func);
    WR32(pbegin, nt);
    WR32(pend, nt + (count + 1u) * 4u);
    ret_c(C, func);
}

/* ---- the varargs family, on the walker --------------------------------- */

void imp_MSVCR71__vsnprintf(CPU *C)
{
    /* (buf, count, fmt, va_list) -- cdecl */
    uint32_t buf = A(0), count = A(1);
    int n = guest_vformat(AS(0), count, ACS(2), A(3));
    ret_c(C, (uint32_t)msvc_trunc(n, buf, count));
}

void imp_MSVCR71__snprintf(CPU *C)
{
    /* (buf, count, fmt, ...) -- the variadic args start at slot 3 */
    uint32_t buf = A(0), count = A(1);
    int n = guest_vformat(AS(0), count, ACS(2), C->esp + 4u + 3u * 4u);
    ret_c(C, (uint32_t)msvc_trunc(n, buf, count));
}

void imp_MSVCR71_vsprintf(CPU *C)
{
    /* (buf, fmt, va_list) -- no bound, which is the caller's problem and the
       reason a huge cap is used rather than a guessed one. */
    int n = guest_vformat(AS(0), 0x7FFFFFFFu, ACS(1), A(2));
    ret_c(C, (uint32_t)n);
}

void imp_MSVCR71_sprintf(CPU *C)
{
    int n = guest_vformat(AS(0), 0x7FFFFFFFu, ACS(1), C->esp + 4u + 2u * 4u);
    ret_c(C, (uint32_t)n);
}

void imp_MSVCR71_printf(CPU *C)
{
    char buf[4096];
    int n = guest_vformat(buf, sizeof buf, ACS(0), C->esp + 4u + 1u * 4u);
    if (n >= 0) fputs(buf, stdout);
    ret_c(C, (uint32_t)n);
}

void imp_MSVCR71_vprintf(CPU *C)
{
    char buf[4096];
    int n = guest_vformat(buf, sizeof buf, ACS(0), A(1));
    if (n >= 0) fputs(buf, stdout);
    ret_c(C, (uint32_t)n);
}


/* ---- the scanf walker --------------------------------------------------
 *
 * The mirror of guest_vformat: one directive at a time, with the host's sscanf
 * doing the actual conversion and "%n" reporting how much input it consumed, so
 * the position advances by what really matched rather than by a guess.
 *
 * The results go to POINTERS pulled from the guest stack, and the width of each
 * store matters -- writing four bytes for a %hd would corrupt whatever follows
 * it in the guest's struct. So each conversion writes exactly its own size.
 *
 * As with the printf side, a conversion this does not implement STOPS by name.
 * A scanf that silently matches nothing returns a count the caller believes,
 * and the caller then uses uninitialised locals.
 */
int guest_vsscanf(const char *in, const char *fmt, uint32_t va)
{
    int filled = 0, pos = 0, n;
    const char *p = fmt;
    /* Large enough for a real scanset: the engine parses identifiers with
       %[_a-zA-Z0-9./\-] spelled out in full, which is 67 characters. At 64
       this refused with "unterminated scanset" -- the right refusal for the
       wrong reason, and it would have been read as a malformed format in the
       game rather than a small buffer here. */
    char spec[320];
    if (!in || !fmt) {
        crt_unimpl("sscanf", "the input or format pointer is NULL");
        return -1;
    }
    g_vfmt_va = va;
    while (*p) {
        if (isspace((unsigned char)*p)) {
            while (isspace((unsigned char)in[pos])) pos++;
            p++;
            continue;
        }
        if (*p != '%') {
            /* A literal must match, and a mismatch ends the scan -- that is
               how the caller learns the input was not what it expected. */
            if (in[pos] != *p) return filled;
            pos++; p++;
            continue;
        }
        p++;
        if (*p == '%') {
            if (in[pos] != '%') return filled;
            pos++; p++;
            continue;
        }
        {
            int suppress = 0, width = 0, lng = 0, si = 0, consumed = 0;
            if (*p == '*') { suppress = 1; p++; }
            while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
            if (p[0] == 'l' && p[1] == 'l') { lng = 2; p += 2; }
            else if (*p == 'l' || *p == 'L') { lng = 1; p++; }
            else if (*p == 'h') { lng = -1; p++; }

            spec[si++] = '%';
            if (width) si += snprintf(spec + si, sizeof spec - si, "%d", width);

            switch (*p) {
            case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
                long long v = 0;
                spec[si++] = 'l'; spec[si++] = 'l'; spec[si++] = *p;
                snprintf(spec + si, sizeof spec - si, "%%n");
                n = sscanf(in + pos, spec, &v, &consumed);
                if (n < 1) return filled;
                pos += consumed;
                if (!suppress) {
                    uint32_t dst = va_dword();
                    if (lng == 2) { WR32(dst, (uint32_t)v);
                                    WR32(dst + 4u, (uint32_t)((uint64_t)v >> 32)); }
                    else if (lng == -1) WR16(dst, (uint16_t)v);
                    else WR32(dst, (uint32_t)v);
                    filled++;
                }
                break;
            }
            case 'f': case 'e': case 'g': case 'E': case 'G': {
                double v = 0;
                spec[si++] = 'l'; spec[si++] = 'f';
                snprintf(spec + si, sizeof spec - si, "%%n");
                n = sscanf(in + pos, spec, &v, &consumed);
                if (n < 1) return filled;
                pos += consumed;
                if (!suppress) {
                    uint32_t dst = va_dword();
                    /* `%f` stores a float and `%lf` a double -- a four-byte
                       store for a double would leave half the value behind. */
                    if (lng) { double dv = v; memcpy((void *)(uintptr_t)dst, &dv, 8); }
                    else     { float fv = (float)v;
                               memcpy((void *)(uintptr_t)dst, &fv, 4); }
                    filled++;
                }
                break;
            }
            case 's': {
                char buf[512];
                snprintf(spec + si, sizeof spec - si, "s%%n");
                n = sscanf(in + pos, spec, buf, &consumed);
                if (n < 1) return filled;
                pos += consumed;
                if (!suppress) {
                    uint32_t dst = va_dword();
                    memcpy((void *)(uintptr_t)dst, buf, strlen(buf) + 1);
                    filled++;
                }
                break;
            }
            case '[': {
                /* A scanset, `%[abc]` or `%[^\n]`. The host's sscanf
                   implements these, so the bracket expression is copied
                   through verbatim -- including the two places where a `]` is
                   a literal member rather than the terminator: immediately
                   after `[` and immediately after `[^`. */
                char buf[512];
                const char *q = p + 1;
                int depth_ok = 0;
                spec[si++] = '[';
                if (*q == '^') spec[si++] = *q++;
                if (*q == ']') spec[si++] = *q++;
                while (*q && *q != ']') {
                    if (si >= (int)sizeof spec - 8) break;
                    spec[si++] = *q++;
                }
                if (*q == ']') { spec[si++] = ']'; q++; depth_ok = 1; }
                if (!depth_ok) {
                    fprintf(stderr, "crt: scanf walker met an unterminated "
                                    "%%[ scanset in format \"%s\" -- "
                                    "refusing\n", fmt);
                    crt_unimpl("sscanf", "unterminated scanset");
                    return -1;
                }
                snprintf(spec + si, sizeof spec - si, "%%n");
                n = sscanf(in + pos, spec, buf, &consumed);
                if (n < 1) return filled;
                pos += consumed;
                if (!suppress) {
                    uint32_t dst = va_dword();
                    memcpy((void *)(uintptr_t)dst, buf, strlen(buf) + 1);
                    filled++;
                }
                p = q - 1;      /* the switch advances past *p below */
                break;
            }
            case 'c': {
                int w = width ? width : 1, k;
                if (!in[pos]) return filled;
                if (!suppress) {
                    uint32_t dst = va_dword();
                    for (k = 0; k < w && in[pos + k]; k++)
                        WR8(dst + (uint32_t)k, (uint8_t)in[pos + k]);
                    filled++;
                }
                for (k = 0; k < w && in[pos]; k++) pos++;
                break;
            }
            default:
                fprintf(stderr, "crt: scanf walker met %%%c in format \"%s\", "
                                "which it does not implement -- refusing rather "
                                "than reporting a match it did not make\n",
                        *p ? *p : '?', fmt);
                crt_unimpl("sscanf", "unimplemented scanf conversion");
                return -1;
            }
            p++;
        }
    }
    return filled;
}

void imp_MSVCR71_sscanf(CPU *C)
{
    ret_c(C, (uint32_t)guest_vsscanf(ACS(0), ACS(1), C->esp + 4u + 2u * 4u));
}

void imp_MSVCR71_fscanf(CPU *C)
{
    /* Line-oriented: read one line and scan it. Not identical to C's fscanf,
       which can stop mid-line and leave the rest for the next call -- so the
       difference is stated here rather than discovered. The engine's uses are
       line-based config parsing. */
    char line[1024];
    if (!fgets(line, sizeof line, fh(A(0)))) { ret_c(C, 0xFFFFFFFFu); return; }
    ret_c(C, (uint32_t)guest_vsscanf(line, ACS(1), C->esp + 4u + 2u * 4u));
}

#define CRT_ALIAS(n) \
    void imp_MSVCRT_##n(CPU *C) { imp_MSVCR71_##n(C); }

CRT_ALIAS(malloc)   CRT_ALIAS(calloc)   CRT_ALIAS(realloc)
CRT_ALIAS(memcpy)   CRT_ALIAS(memset)   CRT_ALIAS(memmove)
CRT_ALIAS(strlen)   CRT_ALIAS(strcpy)   CRT_ALIAS(strcat)
CRT_ALIAS(strcmp)   CRT_ALIAS(strchr)   CRT_ALIAS(strrchr)
CRT_ALIAS(strstr)   CRT_ALIAS(strtok)   CRT_ALIAS(strncat)
CRT_ALIAS(strncpy)  CRT_ALIAS(strncmp)  CRT_ALIAS(_strdup)
CRT_ALIAS(_stricmp) CRT_ALIAS(_strnicmp) CRT_ALIAS(_strlwr)
CRT_ALIAS(_strupr)  CRT_ALIAS(_itoa)
CRT_ALIAS(isalnum)  CRT_ALIAS(isalpha)  CRT_ALIAS(isdigit)
CRT_ALIAS(isspace)  CRT_ALIAS(tolower)
CRT_ALIAS(atoi)     CRT_ALIAS(atof)     CRT_ALIAS(strtod)
CRT_ALIAS(ceil)     CRT_ALIAS(floor)    CRT_ALIAS(fabs)
CRT_ALIAS(atan2)    CRT_ALIAS(exp)      CRT_ALIAS(log)
CRT_ALIAS(pow)      CRT_ALIAS(sqrt)     CRT_ALIAS(_finite)
CRT_ALIAS(_CIpow)   CRT_ALIAS(_CIfmod)  CRT_ALIAS(_CIacos)
CRT_ALIAS(_CIasin)
CRT_ALIAS(rand)     CRT_ALIAS(srand)    CRT_ALIAS(qsort)
CRT_ALIAS(fopen)    CRT_ALIAS(fclose)   CRT_ALIAS(fread)
CRT_ALIAS(fseek)    CRT_ALIAS(ftell)
CRT_ALIAS(exit)     CRT_ALIAS(_exit)    CRT_ALIAS(_purecall)
CRT_ALIAS(abort)    CRT_ALIAS(_errno)   CRT_ALIAS(getenv)
/* the varargs family, on the format walker */
CRT_ALIAS(printf)   CRT_ALIAS(vprintf)  CRT_ALIAS(sprintf)
CRT_ALIAS(vsprintf) CRT_ALIAS(_snprintf) CRT_ALIAS(_vsnprintf)
CRT_ALIAS(fflush)   CRT_ALIAS(fputc)    CRT_ALIAS(fputs)
CRT_ALIAS(fgetc)    CRT_ALIAS(fgets)    CRT_ALIAS(ungetc)
CRT_ALIAS(fwrite)   CRT_ALIAS(fprintf)  CRT_ALIAS(vfprintf)
/* C++ operator new / delete / delete[] */
CRT_ALIAS(__2_YAPAXI_Z) CRT_ALIAS(__3_YAXPAX_Z) CRT_ALIAS(__V_YAXPAX_Z)
CRT_ALIAS(sscanf)   CRT_ALIAS(fscanf)
CRT_ALIAS(setlocale) CRT_ALIAS(_onexit)
CRT_ALIAS(free)     CRT_ALIAS(_ftol)    CRT_ALIAS(_initterm)
CRT_ALIAS(__dllonexit)
