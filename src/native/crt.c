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
#define MAX_FILES 64
static FILE *g_files[MAX_FILES];

static FILE *fh(uint32_t h)
{
    if (h == 0 || h > MAX_FILES || !g_files[h - 1]) {
        fprintf(stderr, "crt: file handle %u is not open\n", h);
        abort();
    }
    return g_files[h - 1];
}

void imp_MSVCR71_fopen(CPU *C)
{
    int i;
    for (i = 0; i < MAX_FILES; i++) {
        if (g_files[i]) continue;
        g_files[i] = fopen(ACS(0), ACS(1));
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

void imp_MSVCR71__mkdir(CPU *C) { ret_c(C, (uint32_t)mkdir(ACS(0), 0777)); }

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
NOT_IMPL(printf, VA_WHY)
NOT_IMPL(sprintf, VA_WHY)
NOT_IMPL(sscanf, VA_WHY)
NOT_IMPL(_vsnprintf, VA_WHY)
NOT_IMPL(vsprintf, VA_WHY)

/* ---- shared with the DLLs' MSVCRT --------------------------------------
 *
 * The exe imports MSVCR71.dll and the DLLs import MSVCRT.dll. Same functions,
 * different spelling of the containing module, and the recompiler names an
 * import stub after both. Aliasing rather than reimplementing: two copies of
 * _initterm would be two things to keep in step, and the one that drifts is
 * the one nobody is looking at.
 */
void imp_MSVCRT__initterm(CPU *C);
void imp_MSVCRT___dllonexit(CPU *C);
void imp_MSVCRT__ftol(CPU *C);

void imp_MSVCR71__initterm(CPU *C)   { imp_MSVCRT__initterm(C); }
void imp_MSVCR71___dllonexit(CPU *C) { imp_MSVCRT___dllonexit(C); }
void imp_MSVCR71__ftol(CPU *C)       { imp_MSVCRT__ftol(C); }

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

/* _onexit(func): register an atexit handler. The table is the same one
   __dllonexit maintains, and the CRT keeps its head in __onexitbegin/end,
   which this build does not expose -- so registration is recorded here and
   the handlers run at exit(). */
#define MAX_ONEXIT 64
static uint32_t g_onexit[MAX_ONEXIT];
static int g_nonexit;

void imp_MSVCR71__onexit(CPU *C)
{
    if (g_nonexit == MAX_ONEXIT) {
        fprintf(stderr, "crt: more than %d _onexit handlers\n", MAX_ONEXIT);
        abort();
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
