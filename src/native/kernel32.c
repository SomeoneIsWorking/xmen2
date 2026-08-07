/*
 * KERNEL32 -- the 39 entry points XMen2.exe imports, on POSIX.
 *
 * Same rule as the rest of the native layer: implement it or stop by name.
 * The one thing that would be easy and wrong here is a "success" return from a
 * function that did nothing -- CreateFileA returning a handle it never opened,
 * GetFileAttributesA claiming a file exists. Those turn a missing feature into
 * corrupt data much later, so each of these either does the work or does not
 * exist yet.
 *
 * Handles. A HANDLE must fit in a guest pointer, and it must not collide with
 * the pseudo-handles Win32 defines, so the guest gets a small index into a
 * table kept here. INVALID_HANDLE_VALUE is (HANDLE)-1 and 0 is "failed", which
 * is why indices start at 1.
 *
 * Paths. The game ships Windows paths with backslashes and a case that does
 * not match the extracted files. Both are translated in one place, win_path(),
 * rather than at each call site -- see the note there for why case-insensitive
 * resolution is a correctness requirement and not a convenience.
 */
#include "x86rt.h"
#include "guest_heap.h"
#include "x86rt_native.h"
#include "pe_map.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define A(i)  RD32(C->esp + 4u + (uint32_t)(i) * 4u)
#define ACS(i) ((const char *)(uintptr_t)A(i))

static void ret_std(CPU *C, uint32_t eax, int nargs)
{
    C->eax = eax;
    C->esp += 4u + (uint32_t)nargs * 4u;
}

static void k32_unimpl(const char *sym, const char *why)
{
    fprintf(stderr, "kernel32: %s is reached but not implemented.\n  %s\n",
            sym, why);
    /* Report before stopping: abort() does not run atexit handlers, so without
       this the reached set and the argument watch stay silent on exactly the
       stop the reader is looking at. */
    x86_diag_dump();
    abort();
}

/* ---- handles ----------------------------------------------------------- */

#define MAX_HANDLES 256
#define H_FILE 1
#define H_FIND 2
#define H_MAP  3
#define H_SEM  4
#define H_EVENT 5
#define H_MUTEX 6
#define H_THREAD 7

typedef struct {
    int   kind;
    int   fd;
    DIR  *dir;
    char  pattern[256];
    char  dirpath[1024];
    void *map;
    size_t maplen;
    /* Synchronisation objects. Nothing here creates a guest thread yet, so the
       state is kept honestly and never actually blocks -- see the wait. */
    int32_t  count;      /* semaphore count, or event signalled, or mutex depth */
    int32_t  maxcount;   /* semaphore ceiling */
    int      manual;     /* event: manual-reset rather than auto-reset */
    char     name[128];
} Handle;

static Handle g_h[MAX_HANDLES];

static uint32_t h_alloc(int kind)
{
    int i;
    for (i = 0; i < MAX_HANDLES; i++)
        if (!g_h[i].kind) { memset(&g_h[i], 0, sizeof g_h[i]);
                            g_h[i].kind = kind; g_h[i].fd = -1;
                            return (uint32_t)(i + 1); }
    fprintf(stderr, "kernel32: more than %d handles open at once\n", MAX_HANDLES);
    abort();
}

static Handle *h_get(uint32_t h, int kind)
{
    if (h == 0 || h > MAX_HANDLES || !g_h[h - 1].kind) {
        fprintf(stderr, "kernel32: handle %u is not open\n", h);
        x86_diag_dump();
        abort();
    }
    if (kind && g_h[h - 1].kind != kind) {
        fprintf(stderr, "kernel32: handle %u is the wrong kind (%d, wanted %d)"
                        "\n", h, g_h[h - 1].kind, kind);
        abort();
    }
    return &g_h[h - 1];
}

/* ---- paths -------------------------------------------------------------
 *
 * Backslashes become slashes, and the result is resolved case-insensitively
 * one component at a time.
 *
 * The case folding is not a nicety. The game asks for paths whose case does
 * not match what is on disk (Windows never cared), and a failed open here does
 * not surface as "file not found" -- it surfaces as a missing texture or an
 * empty archive much later, with nothing pointing back at the path. Resolving
 * it once, here, is far cheaper than diagnosing that.
 */
static int resolve_ci(char *path)
{
    char *p, *comp, saved;
    DIR *d;
    struct dirent *e;
    if (access(path, F_OK) == 0) return 1;
    p = path;
    if (*p == '/') p++;
    while (p && *p) {
        comp = strchr(p, '/');
        if (comp) { saved = *comp; *comp = '\0'; }
        if (access(path, F_OK) != 0) {
            char *slash = strrchr(path, '/');
            const char *dirp = ".";
            if (slash) { *slash = '\0'; dirp = path[0] ? path : "/"; }
            d = opendir(dirp);
            if (slash) *slash = '/';
            if (!d) { if (comp) *comp = saved; return 0; }
            for (e = readdir(d); e; e = readdir(d))
                if (strcasecmp(e->d_name, p) == 0) {
                    memcpy(p, e->d_name, strlen(p));
                    break;
                }
            closedir(d);
            if (!e) { if (comp) *comp = saved; return 0; }
        }
        if (comp) { *comp = saved; p = comp + 1; } else break;
    }
    return access(path, F_OK) == 0;
}

/* Shared with the CRT: crt.c's fopen was passing the guest's path straight to
   the host, so a "Data\\foo.XMLB" never opened and surfaced as a missing asset
   rather than as a failed open. One translation, one place. */
const char *win_path(const char *in)
{
    static char buf[1024];
    const char *game = getenv("GAME_PC_DIR");
    size_t i;
    if (!in) return NULL;
    /* A drive letter or a leading slash means an absolute Windows path; the
       game uses relative ones for its own data, resolved against the install. */
    if (in[0] && in[1] == ':')
        snprintf(buf, sizeof buf, "%s/%s", game ? game : ".", in + 2);
    else if (in[0] == '\\' || in[0] == '/')
        snprintf(buf, sizeof buf, "%s/%s", game ? game : ".", in + 1);
    else
        snprintf(buf, sizeof buf, "%s/%s", game ? game : ".", in);
    for (i = 0; buf[i]; i++) if (buf[i] == '\\') buf[i] = '/';
    resolve_ci(buf);
    return buf;
}

/* ---- error reporting --------------------------------------------------- */

static uint32_t g_last_error;
static uint64_t g_reserved_bytes;   /* see VirtualAlloc */

void imp_KERNEL32_GetLastError(CPU *C) { ret_std(C, g_last_error, 0); }

#define ERROR_FILE_NOT_FOUND 2u
#define ERROR_NO_MORE_FILES  18u
#define INVALID_HANDLE       0xFFFFFFFFu

/* ---- time and identity ------------------------------------------------- */

void imp_KERNEL32_GetSystemTimeAsFileTime(CPU *C)
{
    /* FILETIME: 100-ns ticks since 1601-01-01. The offset to the Unix epoch is
       exact, so this is a real conversion rather than an approximation. */
    struct timeval tv;
    uint64_t ft;
    gettimeofday(&tv, NULL);
    ft = ((uint64_t)tv.tv_sec + 11644473600ULL) * 10000000ULL
         + (uint64_t)tv.tv_usec * 10ULL;
    WR32(A(0), (uint32_t)ft);
    WR32(A(0) + 4u, (uint32_t)(ft >> 32));
    ret_std(C, 0, 1);
}

void imp_KERNEL32_GetTickCount(CPU *C)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ret_std(C, (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000), 0);
}

void imp_KERNEL32_QueryPerformanceCounter(CPU *C)
{
    struct timespec ts;
    uint64_t v;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    v = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    WR32(A(0), (uint32_t)v);
    WR32(A(0) + 4u, (uint32_t)(v >> 32));
    ret_std(C, 1, 1);
}

void imp_KERNEL32_GetCurrentProcessId(CPU *C) { ret_std(C, (uint32_t)getpid(), 0); }
void imp_KERNEL32_GetCurrentThreadId(CPU *C)  { ret_std(C, (uint32_t)gettid(), 0); }
void imp_KERNEL32_Sleep(CPU *C) { usleep(A(0) * 1000u); ret_std(C, 0, 1); }

void imp_KERNEL32_GetVersionExA(CPU *C)
{
    /* OSVERSIONINFOA: report Windows XP (5.1.2600). The game branches on this
       for feature checks, and the honest answer is the OS whose API this layer
       implements -- not the host's, which has no version in these terms. */
    uint32_t p = A(0);
    WR32(p + 4u, 5);                     /* dwMajorVersion */
    WR32(p + 8u, 1);                     /* dwMinorVersion */
    WR32(p + 12u, 2600);                 /* dwBuildNumber */
    WR32(p + 16u, 2);                    /* VER_PLATFORM_WIN32_NT */
    *(volatile char *)(uintptr_t)(p + 20u) = '\0';   /* szCSDVersion */
    ret_std(C, 1, 1);
}

void imp_KERNEL32_IsProcessorFeaturePresent(CPU *C)
{
    /* 0 = FP error, 1 = 80387, 2 = compare-exchange, 3 = MMX, 6 = SSE,
       10 = SSE2. This build runs on the host CPU, so the truthful answer for
       the ones the game asks about is yes -- and saying no to SSE would send
       it down a path the recompiler has LESS coverage of, not more. */
    uint32_t f = A(0);
    ret_std(C, (f <= 10u) ? 1u : 0u, 1);
}

void imp_KERNEL32_GetStartupInfoA(CPU *C)
{
    uint32_t p = A(0);
    memset((void *)(uintptr_t)p, 0, 68);
    WR32(p, 68);                         /* cb */
    ret_std(C, 0, 1);
}

void imp_KERNEL32_ExitProcess(CPU *C) { exit((int)A(0)); }

/* ---- critical sections -------------------------------------------------
 *
 * Single-threaded for now: nothing here creates a guest thread, so a critical
 * section has nothing to serialise. They are still tracked rather than ignored
 * -- Enter/Leave keep a depth per section, so an unbalanced pair is caught
 * here instead of becoming a deadlock the day threads exist.
 */
void imp_KERNEL32_InitializeCriticalSection(CPU *C) { WR32(A(0) + 4u, 0); ret_std(C, 0, 1); }
void imp_KERNEL32_DeleteCriticalSection(CPU *C)
{
    if (RD32(A(0) + 4u) != 0)
        fprintf(stderr, "kernel32: DeleteCriticalSection on one still entered "
                        "%u time(s)\n", RD32(A(0) + 4u));
    ret_std(C, 0, 1);
}
void imp_KERNEL32_EnterCriticalSection(CPU *C)
{
    WR32(A(0) + 4u, RD32(A(0) + 4u) + 1u);
    ret_std(C, 0, 1);
}
void imp_KERNEL32_LeaveCriticalSection(CPU *C)
{
    uint32_t d = RD32(A(0) + 4u);
    if (d == 0) {
        fprintf(stderr, "kernel32: LeaveCriticalSection on one that was never "
                        "entered\n");
        abort();
    }
    WR32(A(0) + 4u, d - 1u);
    ret_std(C, 0, 1);
}

/* ---- files ------------------------------------------------------------- */

#define GENERIC_WRITE 0x40000000u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u

void imp_KERNEL32_CreateFileA(CPU *C)
{
    uint32_t access_ = A(1), disp = A(4), h;
    const char *path = win_path(ACS(0));
    int flags = (access_ & GENERIC_WRITE) ? O_RDWR : O_RDONLY;
    int fd;
    if (disp == CREATE_ALWAYS) flags = O_RDWR | O_CREAT | O_TRUNC;
    fd = open(path, flags, 0644);
    if (fd < 0) {
        g_last_error = ERROR_FILE_NOT_FOUND;
        ret_std(C, INVALID_HANDLE, 7);
        return;
    }
    h = h_alloc(H_FILE);
    g_h[h - 1].fd = fd;
    ret_std(C, h, 7);
}

void imp_KERNEL32_ReadFile(CPU *C)
{
    Handle *hh = h_get(A(0), H_FILE);
    ssize_t n = read(hh->fd, (void *)(uintptr_t)A(1), A(2));
    if (A(3)) WR32(A(3), n < 0 ? 0u : (uint32_t)n);
    ret_std(C, n < 0 ? 0u : 1u, 5);
}

void imp_KERNEL32_WriteFile(CPU *C)
{
    Handle *hh = h_get(A(0), H_FILE);
    ssize_t n = write(hh->fd, (const void *)(uintptr_t)A(1), A(2));
    if (A(3)) WR32(A(3), n < 0 ? 0u : (uint32_t)n);
    ret_std(C, n < 0 ? 0u : 1u, 5);
}

void imp_KERNEL32_GetFileSize(CPU *C)
{
    Handle *hh = h_get(A(0), H_FILE);
    struct stat st;
    if (fstat(hh->fd, &st) != 0) { ret_std(C, 0xFFFFFFFFu, 2); return; }
    if (A(1)) WR32(A(1), (uint32_t)((uint64_t)st.st_size >> 32));
    ret_std(C, (uint32_t)st.st_size, 2);
}

void imp_KERNEL32_SetEndOfFile(CPU *C)
{
    Handle *hh = h_get(A(0), H_FILE);
    off_t at = lseek(hh->fd, 0, SEEK_CUR);
    ret_std(C, ftruncate(hh->fd, at) == 0 ? 1u : 0u, 1);
}

/* ---- synchronisation objects -------------------------------------------
 *
 * Semaphores, events and mutexes, on the same terms as the critical sections
 * above: the STATE is modelled exactly, and nothing blocks, because nothing in
 * this process creates a guest thread yet. That is the honest position -- a
 * wait that cannot be satisfied has no other thread that could ever satisfy
 * it, so pretending it succeeded would hand the guest a lock it does not hold
 * and the damage would surface somewhere unrelated.
 *
 * So a wait that would block says so and stops. When threads exist these
 * become real POSIX primitives and the refusal goes away; until then it is the
 * difference between "not implemented yet" and "quietly wrong".
 */
#define WAIT_OBJECT_0   0x00000000u
#define WAIT_TIMEOUT    0x00000102u
#define WAIT_FAILED     0xFFFFFFFFu

static void sync_name(Handle *hh, uint32_t namep)
{
    if (namep) snprintf(hh->name, sizeof hh->name, "%s",
                        (const char *)(uintptr_t)namep);
    else       snprintf(hh->name, sizeof hh->name, "%s", "(unnamed)");
}

void imp_KERNEL32_CreateSemaphoreA(CPU *C)
{
    /* (attrs, lInitialCount, lMaximumCount, name) */
    uint32_t h = h_alloc(H_SEM);
    Handle *hh = &g_h[h - 1];
    hh->count = (int32_t)A(1);
    hh->maxcount = (int32_t)A(2);
    sync_name(hh, A(3));
    ret_std(C, h, 4);
}

void imp_KERNEL32_ReleaseSemaphore(CPU *C)
{
    /* (handle, lReleaseCount, lpPreviousCount) */
    Handle *hh = h_get(A(0), H_SEM);
    int32_t prev = hh->count;
    if (A(1) == 0 || (int64_t)hh->count + (int32_t)A(1) > hh->maxcount) {
        g_last_error = 87u;                       /* ERROR_INVALID_PARAMETER */
        ret_std(C, 0, 3);
        return;
    }
    hh->count += (int32_t)A(1);
    if (A(2)) WR32(A(2), (uint32_t)prev);
    ret_std(C, 1, 3);
}

void imp_KERNEL32_CreateEventA(CPU *C)
{
    /* (attrs, bManualReset, bInitialState, name) */
    uint32_t h = h_alloc(H_EVENT);
    Handle *hh = &g_h[h - 1];
    hh->manual = (int)A(1);
    hh->count = A(2) ? 1 : 0;
    sync_name(hh, A(3));
    ret_std(C, h, 4);
}

void imp_KERNEL32_SetEvent(CPU *C)
{
    h_get(A(0), H_EVENT)->count = 1;
    ret_std(C, 1, 1);
}

void imp_KERNEL32_ResetEvent(CPU *C)
{
    h_get(A(0), H_EVENT)->count = 0;
    ret_std(C, 1, 1);
}

void imp_KERNEL32_CreateMutexA(CPU *C)
{
    /* (attrs, bInitialOwner, name) */
    uint32_t h = h_alloc(H_MUTEX);
    Handle *hh = &g_h[h - 1];
    hh->count = A(1) ? 1 : 0;                     /* recursion depth held */
    sync_name(hh, A(2));
    ret_std(C, h, 3);
}

void imp_KERNEL32_ReleaseMutex(CPU *C)
{
    Handle *hh = h_get(A(0), H_MUTEX);
    if (hh->count <= 0) {
        fprintf(stderr, "kernel32: ReleaseMutex on %s, which this thread does "
                        "not hold\n", hh->name);
        g_last_error = 288u;                      /* ERROR_NOT_OWNER */
        ret_std(C, 0, 1);
        return;
    }
    hh->count--;
    ret_std(C, 1, 1);
}

/* Try to take one object. Returns 1 if it was signalled (and consumes it). */
static int sync_try_take(Handle *hh)
{
    switch (hh->kind) {
    case H_SEM:
        if (hh->count > 0) { hh->count--; return 1; }
        return 0;
    case H_EVENT:
        if (hh->count) { if (!hh->manual) hh->count = 0; return 1; }
        return 0;
    case H_MUTEX:
        /* Single-threaded: this thread is the only one, so an unheld mutex is
           acquirable and a held one is held BY US -- Win32 mutexes are
           recursive for the owning thread. */
        hh->count++;
        return 1;
    default:
        return 0;
    }
}

static const char *sync_kind_name(int k)
{
    return k == H_SEM ? "semaphore" : k == H_EVENT ? "event"
         : k == H_MUTEX ? "mutex" : "non-waitable object";
}

void imp_KERNEL32_WaitForSingleObject(CPU *C)
{
    Handle *hh = h_get(A(0), 0);
    uint32_t ms = A(1);
    if (sync_try_take(hh)) { ret_std(C, WAIT_OBJECT_0, 2); return; }
    if (ms == 0) { ret_std(C, WAIT_TIMEOUT, 2); return; }
    fprintf(stderr,
        "kernel32: WaitForSingleObject would BLOCK on %s \"%s\" (timeout %u).\n"
        "  This process has no guest threads, so nothing could ever signal it "
        "and the wait cannot be satisfied.\n"
        "  Returning success here would hand the game a lock it does not hold; "
        "returning timeout would be a lie about a wait that never happened.\n"
        "  This is the point at which real threads are needed.\n",
        sync_kind_name(hh->kind), hh->name, ms);
    abort();
}

void imp_KERNEL32_WaitForMultipleObjects(CPU *C)
{
    /* (nCount, lpHandles, bWaitAll, dwMilliseconds) */
    uint32_t n = A(0), arr = A(1), all = A(2), ms = A(3), i;
    if (n == 0 || n > MAX_HANDLES) {
        g_last_error = 87u;
        ret_std(C, WAIT_FAILED, 4);
        return;
    }
    if (!all) {
        for (i = 0; i < n; i++) {
            Handle *hh = h_get(RD32(arr + i * 4u), 0);
            if (sync_try_take(hh)) { ret_std(C, WAIT_OBJECT_0 + i, 4); return; }
        }
    } else {
        /* All-or-nothing: only take them if every one is ready, so a partial
           take cannot leave the set half-consumed. */
        int ready = 1;
        for (i = 0; i < n; i++) {
            Handle *hh = h_get(RD32(arr + i * 4u), 0);
            if (hh->kind == H_MUTEX) continue;             /* always takeable */
            if (hh->count <= 0) { ready = 0; break; }
        }
        if (ready) {
            for (i = 0; i < n; i++) sync_try_take(h_get(RD32(arr + i * 4u), 0));
            ret_std(C, WAIT_OBJECT_0, 4);
            return;
        }
    }
    if (ms == 0) { ret_std(C, WAIT_TIMEOUT, 4); return; }
    fprintf(stderr,
        "kernel32: WaitForMultipleObjects would BLOCK on %u object(s) "
        "(waitAll=%u, timeout %u).\n"
        "  No guest thread exists to signal them -- see WaitForSingleObject.\n",
        n, all, ms);
    abort();
}

/* ---- thread-local storage ----------------------------------------------
 *
 * One process-wide array, because this process has one guest thread. That is
 * the whole implementation and it is exactly right until a second thread
 * exists -- at which point these become pthread_key_t and the array is the bug
 * to remove, so it says so here rather than in a commit message.
 */
#define MAX_TLS 128
static uint32_t g_tls[MAX_TLS];
static unsigned char g_tls_used[MAX_TLS];
#define TLS_OUT_OF_INDEXES 0xFFFFFFFFu

void imp_KERNEL32_TlsAlloc(CPU *C)
{
    unsigned i;
    for (i = 0; i < MAX_TLS; i++)
        if (!g_tls_used[i]) {
            g_tls_used[i] = 1;
            g_tls[i] = 0;                     /* Win32 guarantees zero */
            ret_std(C, i, 0);
            return;
        }
    g_last_error = 87u;
    ret_std(C, TLS_OUT_OF_INDEXES, 0);
}

void imp_KERNEL32_TlsFree(CPU *C)
{
    uint32_t i = A(0);
    if (i >= MAX_TLS || !g_tls_used[i]) {
        fprintf(stderr, "kernel32: TlsFree(%u) on an index that was never "
                        "allocated\n", i);
        g_last_error = 87u;
        ret_std(C, 0, 1);
        return;
    }
    g_tls_used[i] = 0;
    ret_std(C, 1, 1);
}

void imp_KERNEL32_TlsGetValue(CPU *C)
{
    uint32_t i = A(0);
    if (i >= MAX_TLS || !g_tls_used[i]) {
        /* Not a fatal error in Win32 -- it sets last-error and returns 0 --
           but it is always a bug in the caller, so it is reported. */
        fprintf(stderr, "kernel32: TlsGetValue(%u) on an unallocated index\n", i);
        g_last_error = 87u;
        ret_std(C, 0, 1);
        return;
    }
    g_last_error = 0;
    ret_std(C, g_tls[i], 1);
}

void imp_KERNEL32_TlsSetValue(CPU *C)
{
    uint32_t i = A(0);
    if (i >= MAX_TLS || !g_tls_used[i]) {
        fprintf(stderr, "kernel32: TlsSetValue(%u) on an unallocated index\n", i);
        g_last_error = 87u;
        ret_std(C, 0, 2);
        return;
    }
    g_tls[i] = A(1);
    ret_std(C, 1, 2);
}

/* ---- odds and ends, each actually implemented -------------------------- */

void imp_KERNEL32_InterlockedExchange(CPU *C)
{
    /* Single guest thread, so a plain swap IS atomic with respect to it. The
       guest memory is also host memory, so this is a real exchange on the real
       word, not a shadow copy. */
    uint32_t p = A(0), old = RD32(p);
    WR32(p, A(1));
    ret_std(C, old, 2);
}

void imp_KERNEL32_QueryPerformanceFrequency(CPU *C)
{
    /* Must agree with QueryPerformanceCounter above, which returns nanoseconds
       from CLOCK_MONOTONIC. A mismatched pair is a timing bug that looks like a
       gameplay bug. */
    WR32(A(0), 1000000000u);
    WR32(A(0) + 4u, 0);
    ret_std(C, 1, 1);
}

/* The Win32 pseudo-handles, which are constants and not table entries. */
void imp_KERNEL32_GetCurrentProcess(CPU *C) { ret_std(C, 0xFFFFFFFFu, 0); }
void imp_KERNEL32_GetCurrentThread(CPU *C)  { ret_std(C, 0xFFFFFFFEu, 0); }

void imp_KERNEL32_OutputDebugStringA(CPU *C)
{
    const char *s2 = ACS(0);
    fprintf(stderr, "[guest] %s", s2 ? s2 : "(null)");
    ret_std(C, 0, 1);
}

/* The Win32 pseudo-handles are constants, not table entries: (HANDLE)-1 is the
   current process and (HANDLE)-2 the current thread. DuplicateHandle on one is
   the standard way to obtain a REAL handle to yourself, so it is turned into a
   table entry here rather than faulting in h_get. */
#define PSEUDO_PROCESS 0xFFFFFFFFu
#define PSEUDO_THREAD  0xFFFFFFFEu

void imp_KERNEL32_DuplicateHandle(CPU *C)
{
    /* (hSrcProc, hSrc, hDstProc, lpDst, access, inherit, options)
       One process here, so "duplicate into another process" is the same table.
       A file handle gets a real dup() -- sharing one fd would make a close on
       either handle break the other, which is precisely the bug a duplicate is
       asked for to avoid. */
    uint32_t src = A(1), dstp = A(3), options = A(6);
    Handle *sh;
    if (src == PSEUDO_THREAD || src == PSEUDO_PROCESS) {
        uint32_t nh = h_alloc(H_THREAD);
        snprintf(g_h[nh - 1].name, sizeof g_h[nh - 1].name, "%s",
                 src == PSEUDO_THREAD ? "current thread" : "current process");
        if (dstp) WR32(dstp, nh);
        ret_std(C, 1, 7);
        return;
    }
    sh = h_get(src, 0);
    uint32_t nh = h_alloc(sh->kind);
    Handle *dh = &g_h[nh - 1];
    *dh = *sh;
    if (sh->kind == H_FILE && sh->fd >= 0) {
        dh->fd = dup(sh->fd);
        if (dh->fd < 0) {
            fprintf(stderr, "kernel32: DuplicateHandle could not dup fd %d: %s\n",
                    sh->fd, strerror(errno));
            dh->kind = 0;
            g_last_error = 8u;
            ret_std(C, 0, 7);
            return;
        }
    } else if (sh->kind == H_FIND || sh->kind == H_MAP) {
        /* A directory stream and a mapping cannot be shared by copying the
           struct -- both would close the same resource. Refuse rather than
           hand back a handle that breaks on the second close. */
        fprintf(stderr, "kernel32: DuplicateHandle on a %s handle is not "
                        "implemented; copying the struct would double-close\n",
                sh->kind == H_FIND ? "find" : "file-mapping");
        dh->kind = 0;
        abort();
    }
    if (dstp) WR32(dstp, nh);
    if (options & 1u) {                  /* DUPLICATE_CLOSE_SOURCE */
        if (sh->kind == H_FILE && sh->fd >= 0) close(sh->fd);
        sh->kind = 0;
    }
    ret_std(C, 1, 7);
}

void imp_KERNEL32_CloseHandle(CPU *C)
{
    Handle *hh = h_get(A(0), 0);
    if (hh->kind == H_FILE && hh->fd >= 0) close(hh->fd);
    if (hh->kind == H_FIND && hh->dir) closedir(hh->dir);
    /* A mapping handle owns its duplicated descriptor. Its VIEWS are not
       unmapped here: on Windows a view outlives the mapping handle, and the
       guest unmaps it explicitly. */
    if (hh->kind == H_MAP && hh->fd >= 0) close(hh->fd);
    hh->kind = 0;
    ret_std(C, 1, 1);
}

void imp_KERNEL32_GetFileAttributesA(CPU *C)
{
    struct stat st;
    const char *p = win_path(ACS(0));
    if (stat(p, &st) != 0) {
        g_last_error = ERROR_FILE_NOT_FOUND;
        ret_std(C, 0xFFFFFFFFu, 1);      /* INVALID_FILE_ATTRIBUTES */
        return;
    }
    ret_std(C, S_ISDIR(st.st_mode) ? 0x10u : 0x80u, 1);   /* DIRECTORY : NORMAL */
}

void imp_KERNEL32_DeleteFileA(CPU *C) { ret_std(C, unlink(win_path(ACS(0))) == 0, 1); }
void imp_KERNEL32_CreateDirectoryA(CPU *C) { ret_std(C, mkdir(win_path(ACS(0)), 0777) == 0, 2); }
void imp_KERNEL32_RemoveDirectoryA(CPU *C) { ret_std(C, rmdir(win_path(ACS(0))) == 0, 1); }

void imp_KERNEL32_GetModuleFileNameA(CPU *C)
{
    /* (hModule, lpFilename, nSize). A HANDLE here is an image base, and every
       module this host mapped came from GAME_PC_DIR, so the real path is
       known. NULL means the exe, which is what the CRT asks for at startup.
       Win32 returns a path with backslashes and the game may parse it, so the
       shape is preserved even though the file lives on a POSIX filesystem. */
    uint32_t h = A(0), buf = A(1), size = A(2);
    const char *dir = getenv("GAME_PC_DIR");
    const char *name = NULL;
    char path[1024];
    uint32_t n, i;
    if (h == 0) name = "XMen2.exe";
    else {
        X86Module *m;
        for (m = x86_modules(); m; m = m->next)
            if (*m->base == h) { name = m->name; break; }
    }
    if (!name) {
        fprintf(stderr, "kernel32: GetModuleFileNameA(0x%08x) -- no mapped "
                        "module has that base, so there is no name to give\n", h);
        g_last_error = 6u;                        /* ERROR_INVALID_HANDLE */
        ret_std(C, 0, 3);
        return;
    }
    snprintf(path, sizeof path, "%s\\%s", dir ? dir : ".", name);
    for (i = 0; path[i]; i++) if (path[i] == '/') path[i] = '\\';
    n = (uint32_t)strlen(path);
    if (size == 0) { ret_std(C, 0, 3); return; }
    if (n >= size) {
        memcpy((void *)(uintptr_t)buf, path, size - 1);
        ((char *)(uintptr_t)buf)[size - 1] = 0;
        g_last_error = 122u;                      /* ERROR_INSUFFICIENT_BUFFER */
        ret_std(C, size, 3);
        return;
    }
    memcpy((void *)(uintptr_t)buf, path, n + 1);
    ret_std(C, n, 3);
}

/* ---- process and thread bookkeeping ------------------------------------
 *
 * Priorities are STORED and returned rather than applied: this host does not
 * reschedule anything, and reporting back a value the caller never set would
 * be the lie. The times are real -- taken from the process clock -- because
 * inventing them would show up as nonsense in any profiling the game does.
 */
static uint32_t g_priority_class = 0x00000020u;   /* NORMAL_PRIORITY_CLASS */
static int32_t  g_thread_priority;                /* THREAD_PRIORITY_NORMAL */

void imp_KERNEL32_GetPriorityClass(CPU *C)  { ret_std(C, g_priority_class, 1); }
void imp_KERNEL32_SetPriorityClass(CPU *C)
{
    g_priority_class = A(1);
    ret_std(C, 1, 2);
}
void imp_KERNEL32_GetThreadPriority(CPU *C) { ret_std(C, (uint32_t)g_thread_priority, 1); }
void imp_KERNEL32_SetThreadPriority(CPU *C)
{
    g_thread_priority = (int32_t)A(1);
    ret_std(C, 1, 2);
}

/* FILETIME from a POSIX clock value in nanoseconds. */
static void wr_filetime(uint32_t p, uint64_t ns)
{
    uint64_t ft = ns / 100ULL;
    if (!p) return;
    WR32(p, (uint32_t)ft);
    WR32(p + 4u, (uint32_t)(ft >> 32));
}

static void times_common(CPU *C, int nargs)
{
    /* (handle, lpCreation, lpExit, lpKernel, lpUser) */
    struct timespec cpu;
    uint64_t ns;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu);
    ns = (uint64_t)cpu.tv_sec * 1000000000ULL + (uint64_t)cpu.tv_nsec;
    wr_filetime(A(1), 0);                 /* creation: process start, unknown */
    wr_filetime(A(2), 0);                 /* exit: still running */
    wr_filetime(A(3), 0);                 /* kernel time not separated here */
    wr_filetime(A(4), ns);                /* user time: the real CPU time */
    ret_std(C, 1, nargs);
}

void imp_KERNEL32_GetProcessTimes(CPU *C) { times_common(C, 5); }
void imp_KERNEL32_GetThreadTimes(CPU *C)  { times_common(C, 5); }

void imp_KERNEL32_FormatMessageA(CPU *C)
{
    /* (flags, source, messageId, langId, lpBuffer, nSize, args)
       There is no Windows message table here. The ID is rendered rather than
       invented as prose: a caller that prints it gets something true and
       traceable instead of a sentence this host made up. */
    uint32_t id = A(2), buf = A(4), size = A(5);
    char msg[64];
    uint32_t n;
    snprintf(msg, sizeof msg, "Win32 error %u", id);
    n = (uint32_t)strlen(msg);
    if (!buf || size <= n) { g_last_error = 122u; ret_std(C, 0, 7); return; }
    memcpy((void *)(uintptr_t)buf, msg, n + 1);
    ret_std(C, n, 7);
}

void imp_KERNEL32_GetSystemDirectoryA(CPU *C)
{
    /* There is no Windows system directory. Returning a path that does not
       exist is better than returning one that does and is not it: anything
       loaded from here would be wrong. */
    const char *s = "C:\\Windows\\System32";
    uint32_t n = (uint32_t)strlen(s);
    if (A(1) > n) memcpy((void *)(uintptr_t)A(0), s, n + 1);
    ret_std(C, n, 2);
}

void imp_KERNEL32_GetDiskFreeSpaceExA(CPU *C)
{
    /* 1 GB free, reported consistently across all three outputs. The game uses
       it to decide whether a save will fit. */
    int i;
    for (i = 1; i <= 3; i++)
        if (A(i)) { WR32(A(i), 0x40000000u); WR32(A(i) + 4u, 0); }
    ret_std(C, 1, 4);
}

/* ---- module handles ---------------------------------------------------- */

/*
 * LoadLibraryA / GetProcAddress, answered from the module table.
 *
 * Every module this host knows is already mapped and recompiled, and a Win32
 * HMODULE *is* the image base -- so for one of ours the honest answer already
 * exists and no loading is required. For anything else there is nothing to
 * return: a fake handle would make GetProcAddress hand back fake functions,
 * which is why this used to refuse outright. It still refuses, but now only
 * for modules that genuinely are not here, and it says which those are.
 */
static uint32_t module_base_by_name(const char *want)
{
    X86Module *m;
    const char *slash;
    char base[128];
    size_t n;
    if (!want) return 0;
    slash = strrchr(want, '\\');
    if (!slash) slash = strrchr(want, '/');
    if (slash) want = slash + 1;
    snprintf(base, sizeof base, "%s", want);
    n = strlen(base);
    /* Win32 appends .dll when the name has no extension; our table holds the
       shipped file names, so compare both ways. */
    for (m = x86_modules(); m; m = m->next) {
        if (strcasecmp(m->name, base) == 0) return *m->base;
        if (strncasecmp(m->name, base, n) == 0 && m->name[n] == '.') return *m->base;
    }
    return 0;
}

/*
 * The SYSTEM DLLs this host implements natively have no image to hand back, so
 * they get a synthetic handle in a range nothing else owns. That is not a fake:
 * GetProcAddress on one resolves through x86_native_thunk, which returns a
 * callable thunk for an entry point that is really implemented and 0 for one
 * that is not -- so a probe for something missing still gets an honest NULL.
 */
#define SYSMOD_BASE 0x000D0000u
#define SYSMOD_STRIDE 0x1000u
#define MAX_SYSMOD 16
static char g_sysmod[MAX_SYSMOD][32];
static int g_nsysmod;

/* Already handed out? GetModuleHandleA must NOT create one: it answers "is
   this module in the address space", and allocating on the way would make the
   answer yes for anything ever asked about. */
static uint32_t sysmod_lookup(const char *name)
{
    int i;
    for (i = 0; i < g_nsysmod; i++)
        if (strcasecmp(g_sysmod[i], name) == 0)
            return SYSMOD_BASE + (uint32_t)i * SYSMOD_STRIDE;
    return 0;
}

static uint32_t sysmod_handle(const char *name)
{
    int i;
    for (i = 0; i < g_nsysmod; i++)
        if (strcasecmp(g_sysmod[i], name) == 0)
            return SYSMOD_BASE + (uint32_t)i * SYSMOD_STRIDE;
    if (g_nsysmod == MAX_SYSMOD) return 0;
    snprintf(g_sysmod[g_nsysmod], sizeof g_sysmod[0], "%s", name);
    g_nsysmod++;
    return SYSMOD_BASE + (uint32_t)(g_nsysmod - 1) * SYSMOD_STRIDE;
}

static const char *sysmod_name(uint32_t h)
{
    uint32_t i;
    if (h < SYSMOD_BASE || h >= SYSMOD_BASE + (uint32_t)MAX_SYSMOD * SYSMOD_STRIDE)
        return NULL;
    i = (h - SYSMOD_BASE) / SYSMOD_STRIDE;
    return (int)i < g_nsysmod ? g_sysmod[i] : NULL;
}

/*
 * The FILE NAME of a module, however it was asked for.
 *
 * Win32 takes a path here and callers use one: XMen2.exe builds
 * "C:\Windows\System32\dinput8.dll" from GetSystemDirectoryA and loads THAT.
 * Comparing the whole string against a module name never matches, so the module
 * was reported missing even when this host implements it (issue #32). Both
 * separators are accepted because a guest string may carry either.
 */
static const char *module_leaf(const char *p)
{
    const char *s;
    if (!p) return NULL;
    for (s = p; *s; s++)
        if (*s == '\\' || *s == '/') p = s + 1;
    return p;
}

/* Which module names this host answers for natively. Anything else has no
   implementation at all, and saying so is the point.

   The list covers modules whose imports are bound statically; a module reached
   only by a run-time lookup declares itself through x86_native_export, and
   asking THAT is what keeps the two from drifting apart. */
static int is_native_sysmod(const char *n)
{
    static const char *known[] = {
        "KERNEL32.DLL", "USER32.DLL", "GDI32.DLL", "ADVAPI32.DLL",
        "MSVCRT.DLL", "MSVCR71.DLL", "SHELL32.DLL", "OLE32.DLL", NULL
    };
    int i;
    if (!n) return 0;
    for (i = 0; known[i]; i++) if (strcasecmp(known[i], n) == 0) return 1;
    return x86_native_module_implemented(n);
}

/*
 * GetModuleHandleA -- "is this module already in the address space, and what
 * is its handle".
 *
 * It lives HERE, beside LoadLibraryA, because the two answer the same question
 * about the same table and must not disagree. They did: this was a separate
 * implementation in win32_sdl.c that knew only the caller's own handle and
 * abort()ed on every other name.
 *
 * What that cost: the game probes GetModuleHandleA("d3d8.dll") /
 * ("d3d8d.dll"), and on success LoadLibraryA + GetProcAddress("DebugSetMute"),
 * to pick up D3D8's DEBUG runtime if it happens to be loaded. Every branch is
 * written for a NULL answer -- the pointers stay null and the game runs
 * without them. The abort turned that ordinary negative into a dead run.
 *
 * NULL is not a fake handle. It is Win32's own answer for a module that is not
 * loaded, it is TRUE here, and the caller checks for it. A handle would be the
 * dishonest reply, because GetProcAddress would then be asked for functions
 * from a module that does not exist.
 */
void imp_KERNEL32_GetModuleHandleA(CPU *C)
{
    const char *nm = module_leaf(ACS(0));
    uint32_t b;
    /* NULL asks for the running program's own handle, which in a PE IS the
       image base -- and that is what the guest uses it as. */
    if (A(0) == 0) { ret_std(C, G_IMGBASE, 1); return; }
    b = module_base_by_name(nm);
    if (!b && nm) b = sysmod_lookup(nm);
    if (!b) {
        /* Reported once per name: a run that answers NULL for a module the
           host was supposed to provide looks identical, from the game's side,
           to one where the module genuinely is absent. */
        static char said[8][32];
        static int nsaid;
        int i;
        for (i = 0; i < nsaid; i++)
            if (strcasecmp(said[i], nm ? nm : "") == 0) break;
        if (i == nsaid && nsaid < 8) {
            snprintf(said[nsaid], sizeof said[0], "%s", nm ? nm : "");
            nsaid++;
            fprintf(stderr, "kernel32: GetModuleHandleA(\"%s\") -> NULL. That "
                            "module is not in this address space.\n"
                            "  This is Win32's own answer for a module that is "
                            "not loaded, and callers check for it -- a handle "
                            "here would\n"
                            "  make GetProcAddress invent functions from a "
                            "module that does not exist.\n", nm ? nm : "(null)");
        }
    }
    ret_std(C, b, 1);
}

void imp_KERNEL32_LoadLibraryA(CPU *C)
{
    const char *nm = module_leaf(ACS(0));
    uint32_t b = module_base_by_name(nm);
    if (b) { ret_std(C, b, 1); return; }
    if (nm && is_native_sysmod(nm)) {
        uint32_t h = sysmod_handle(nm);
        if (h) { ret_std(C, h, 1); return; }
    }
    /*
     * NULL, not abort -- and the distinction is the whole point.
     *
     * A fake HANDLE is what must never be returned: GetProcAddress would then
     * hand back fake functions and the failure would surface far away. NULL is
     * different. It is what Win32 itself returns when a DLL cannot be loaded,
     * it is what every correct caller checks for, and here it is TRUE: this
     * module genuinely is not in the address space.
     *
     * Aborting made that truthful answer unreachable. libIGGfx's initCg
     * (0x1002fa60) loads cg.dll, compares the loader's status against the
     * engine's own failure singleton, and on failure SKIPS the whole Cg setup
     * and returns -- the engine is written to run without Cg. Aborting turned
     * a path the engine handles into a dead stop.
     *
     * Said loudly every time rather than once, because a missing module is not
     * a normal condition and the consequence is specific to which one it was.
     */
    fprintf(stderr, "kernel32: LoadLibraryA(\"%s\") -> NULL. That module is "
                    "not one of the recompiled ones this host has mapped, so "
                    "it genuinely cannot be loaded.\n"
                    "  This is Win32's own failure answer and callers must "
                    "check it; a fake HANDLE is what would be dishonest, "
                    "because GetProcAddress would then invent functions.\n"
                    "  Whatever this module provides is NOT AVAILABLE to the "
                    "game from here on.\n"
                    "  Mapped modules are:", nm ? nm : "(null)");
    {   X86Module *m;
        for (m = x86_modules(); m; m = m->next) fprintf(stderr, " %s", m->name);
    }
    fputc('\n', stderr);
    ret_std(C, 0, 1);
}

void imp_KERNEL32_GetProcAddress(CPU *C)
{
    /* The handle is an image base, so the export table is right there. */
    uint32_t mod = A(0), namep = A(1), rva;
    const char *sym = (const char *)(uintptr_t)namep;
    const char *sm = sysmod_name(mod);
    if (namep && namep < 0x10000u) {
        fprintf(stderr, "kernel32: GetProcAddress by ORDINAL (#%u) is not "
                        "implemented; this layer resolves by name only\n", namep);
        abort();
    }
    if (sm) {
        /* A natively implemented system DLL: hand back a real thunk, or NULL
           if this host does not implement that entry point.

           Two registries, asked in order, because they answer different
           questions: x86_native_thunk finds a symbol some mapped module
           IMPORTS, and x86_native_export_addr finds one this host publishes
           that nothing imports -- which is the only kind a run-time lookup
           like dinput8's can be. */
        uint32_t t = x86_native_thunk(sm, sym);
        if (!t) t = x86_native_export_addr(sm, sym);
        if (t) { ret_std(C, t, 2); return; }
        fprintf(stderr, "kernel32: GetProcAddress(%s, \"%s\") -- this host does "
                        "not implement that entry point, so NULL\n",
                sm, sym ? sym : "(null)");
        g_last_error = 127u;
        ret_std(C, 0, 2);
        return;
    }
    rva = pe_export_rva(mod, sym);
    if (rva) { ret_std(C, mod + rva, 2); return; }
    /* A miss is not fatal in Win32 -- callers probe for optional entry points
       -- so report it and return NULL with the error Win32 would set. */
    fprintf(stderr, "kernel32: GetProcAddress(0x%08x, \"%s\") -> not exported\n",
            mod, sym ? sym : "(null)");
    g_last_error = 127u;                          /* ERROR_PROC_NOT_FOUND */
    ret_std(C, 0, 2);
}

void imp_KERNEL32_FreeLibrary(CPU *C) { ret_std(C, 1, 1); }

/* ---- not yet implemented ----------------------------------------------- */

void imp_KERNEL32_CreateFileW(CPU *C)
{
    k32_unimpl("CreateFileW", "the wide-character file API is unused by the "
               "paths reached so far; implementing it blind would be untested "
               "code on a path nothing exercises");
}

/* ---- file mapping ------------------------------------------------------
 *
 * A real mmap, not a read into a buffer. The guest holds the returned pointer
 * and reads through it, so the view must live below 4 GB where a 32-bit
 * pointer can reach it -- which is the whole difficulty, and why this was
 * unimplemented until there was an arena to put it in.
 *
 * Views are placed by a bump cursor in a region above the guest heap and
 * mapped with MAP_FIXED_NOREPLACE, so a collision with anything already there
 * FAILS rather than silently replacing it. Nothing is reserved up front: the
 * region is an address range this host promises not to use for anything else,
 * not committed memory.
 *
 * Windows requires the view offset to be a multiple of the allocation
 * granularity and mmap requires a multiple of the page size, so the offset is
 * rounded DOWN and the difference added back to the returned pointer -- which
 * is what the guest would get from Windows for the same call.
 */
#define VIEW_ARENA_BASE  0x98000000u   /* above the 512 MB guest heap */
#define VIEW_ARENA_END   0xF0000000u

static uint32_t g_view_cursor = VIEW_ARENA_BASE;

#define MAX_VIEWS 64
static struct { uint32_t addr; size_t len; } g_views[MAX_VIEWS];

void imp_KERNEL32_CreateFileMappingA(CPU *C)
{
    Handle *hf = h_get(A(0), H_FILE);
    uint32_t size_hi = A(3), size_lo = A(4);
    struct stat st;
    uint32_t h;
    int fd;

    if (size_hi) {
        /* A mapping larger than 4 GB cannot be addressed by the guest at all,
           so there is no honest answer -- and silently truncating the size
           would map a prefix and read garbage past it. */
        fprintf(stderr, "kernel32: CreateFileMappingA asked for a %u:%u byte "
                        "mapping. A 32-bit guest cannot address that; "
                        "refusing.\n", size_hi, size_lo);
        g_last_error = 8;                              /* NOT_ENOUGH_MEMORY */
        ret_std(C, 0, 6);
        return;
    }
    if (fstat(hf->fd, &st) != 0) {
        g_last_error = ERROR_FILE_NOT_FOUND;
        ret_std(C, 0, 6);
        return;
    }
    /* The mapping outlives the file handle on Windows, so the descriptor is
       duplicated: the guest closing the file must not unmap the view. */
    fd = dup(hf->fd);
    if (fd < 0) { g_last_error = 8; ret_std(C, 0, 6); return; }

    h = h_alloc(H_MAP);
    g_h[h - 1].fd = fd;
    g_h[h - 1].maplen = size_lo ? (size_t)size_lo : (size_t)st.st_size;
    ret_std(C, h, 6);
}

void imp_KERNEL32_MapViewOfFile(CPU *C)
{
    Handle *hm = h_get(A(0), H_MAP);
    uint32_t off_hi = A(2), off_lo = A(3), want = A(4);
    long page = sysconf(_SC_PAGESIZE);
    uint32_t aligned = off_lo & ~(uint32_t)(page - 1);
    uint32_t delta = off_lo - aligned;
    size_t len = (want ? (size_t)want : hm->maplen - (size_t)off_lo) + delta;
    void *got;
    int i;

    if (off_hi) {
        fprintf(stderr, "kernel32: MapViewOfFile at offset %u:%u -- beyond "
                        "what a 32-bit guest can address.\n", off_hi, off_lo);
        g_last_error = 8;
        ret_std(C, 0, 5);
        return;
    }
    len = (len + (size_t)page - 1) & ~((size_t)page - 1);
    if ((uint64_t)g_view_cursor + len > VIEW_ARENA_END) {
        fprintf(stderr, "kernel32: no room for a %zu byte view: the file-view "
                        "arena 0x%08x..0x%08x is full at 0x%08x.\n",
                len, VIEW_ARENA_BASE, VIEW_ARENA_END, g_view_cursor);
        g_last_error = 8;
        ret_std(C, 0, 5);
        return;
    }
    got = mmap((void *)(uintptr_t)g_view_cursor, len, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_FIXED_NOREPLACE, hm->fd, (off_t)aligned);
    if (got == MAP_FAILED || (uintptr_t)got != (uintptr_t)g_view_cursor) {
        fprintf(stderr, "kernel32: MapViewOfFile could not place a %zu byte "
                        "view at 0x%08x: %s\n", len, g_view_cursor,
                got == MAP_FAILED ? strerror(errno) : "the kernel chose "
                "another address, which the guest could not reach");
        if (got != MAP_FAILED) munmap(got, len);
        g_last_error = 8;
        ret_std(C, 0, 5);
        return;
    }
    for (i = 0; i < MAX_VIEWS; i++)
        if (!g_views[i].addr) {
            g_views[i].addr = g_view_cursor;
            g_views[i].len = len;
            break;
        }
    if (i == MAX_VIEWS)
        fprintf(stderr, "kernel32: more than %d views mapped at once; this one "
                        "cannot be unmapped later.\n", MAX_VIEWS);

    ret_std(C, g_view_cursor + delta, 5);
    g_view_cursor += (uint32_t)len;
}

void imp_KERNEL32_UnmapViewOfFile(CPU *C)
{
    uint32_t addr = A(0);
    int i;
    for (i = 0; i < MAX_VIEWS; i++)
        if (g_views[i].addr && addr >= g_views[i].addr &&
            addr < g_views[i].addr + g_views[i].len) {
            munmap((void *)(uintptr_t)g_views[i].addr, g_views[i].len);
            g_views[i].addr = 0;
            ret_std(C, 1, 1);
            return;
        }
    /* The address space is not reclaimed -- the cursor only goes up. That is
       deliberate for now: reusing a range means a stale guest pointer lands in
       a DIFFERENT file's contents, which reads as corrupt data rather than as
       a use-after-unmap. */
    fprintf(stderr, "kernel32: UnmapViewOfFile(0x%08x) is not the base of any "
                    "view this host mapped.\n", addr);
    ret_std(C, 0, 1);
}

/* ---- directory enumeration ---------------------------------------------
 *
 * FindFirstFileA takes a path with wildcards in its LAST component only, and
 * hands back one entry at a time through a WIN32_FIND_DATAA the caller owns.
 *
 * Three things here are correctness requirements rather than detail:
 *
 * 1. The wildcard is matched with WINDOWS' rules, not fnmatch's. `*.*` on
 *    Windows matches every file, INCLUDING ones with no dot in the name --
 *    fnmatch would drop those silently, and a silently-shorter asset list
 *    surfaces as a missing model, not as a missing file.
 * 2. The match is case-insensitive, for the same reason win_path() resolves
 *    case: the game's patterns do not match the case on disk.
 * 3. The struct is filled COMPLETELY, including the fields this host has
 *    nothing real for. A caller reading an uninitialised cAlternateFileName
 *    gets whatever the guest heap held.
 */
#define FILE_ATTRIBUTE_READONLY   0x00000001u
#define FILE_ATTRIBUTE_DIRECTORY  0x00000010u
#define FILE_ATTRIBUTE_NORMAL     0x00000080u

/* WIN32_FIND_DATAA, by offset. Stated once, here, because every one of these
   is a place a wrong number becomes a wrong file name. */
#define FD_ATTRIBUTES   0u
#define FD_CREATION     4u       /* FILETIME, 8 bytes */
#define FD_LASTACCESS  12u
#define FD_LASTWRITE   20u
#define FD_SIZE_HIGH   28u
#define FD_SIZE_LOW    32u
#define FD_RESERVED0   36u
#define FD_RESERVED1   40u
#define FD_FILENAME    44u       /* char[260] */
#define FD_ALTNAME    304u       /* char[14]  */
#define FD_SIZEOF     320u

/* Windows' own wildcard rules, case-insensitively.

   `*` any run, `?` any single character. `*.*` is special-cased because on
   Windows it means "everything", not "everything with a dot". Not modelled:
   `?` matching ZERO characters at the very end of a name, and the DOS_STAR /
   DOS_DOT forms the kernel derives from short names -- neither appears in a
   pattern a game builds, and inventing them would be guessing. */
static int win_match(const char *pat, const char *name)
{
    if (!strcmp(pat, "*.*") || !strcmp(pat, "*")) return 1;
    while (*pat) {
        if (*pat == '*') {
            pat++;
            if (!*pat) return 1;
            for (; *name; name++)
                if (win_match(pat, name)) return 1;
            return win_match(pat, name);         /* the empty tail */
        }
        if (!*name) return 0;
        if (*pat != '?' &&
            tolower((unsigned char)*pat) != tolower((unsigned char)*name))
            return 0;
        pat++; name++;
    }
    return *name == 0;
}

/* Unix seconds -> FILETIME (100ns ticks since 1601-01-01). */
static void fd_time(uint32_t dst, time_t t)
{
    uint64_t ft = ((uint64_t)t + 11644473600ULL) * 10000000ULL;
    WR32(dst, (uint32_t)ft);
    WR32(dst + 4u, (uint32_t)(ft >> 32));
}

/* Fill the caller's WIN32_FIND_DATAA for one entry. 0 if the entry vanished
   between readdir and stat, which is a race, not a match. */
static int fd_fill(uint32_t data, const char *dirpath, const char *name)
{
    char full[2048];
    struct stat st;
    uint32_t attrs;
    size_t n;

    snprintf(full, sizeof full, "%s/%s", dirpath, name);
    if (stat(full, &st) != 0) return 0;

    attrs = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if (!(st.st_mode & S_IWUSR)) attrs |= FILE_ATTRIBUTE_READONLY;

    memset((void *)(uintptr_t)data, 0, FD_SIZEOF);
    WR32(data + FD_ATTRIBUTES, attrs);
    fd_time(data + FD_CREATION,   st.st_ctime);   /* no birth time on POSIX;
                                                     ctime is the closest
                                                     honest answer */
    fd_time(data + FD_LASTACCESS, st.st_atime);
    fd_time(data + FD_LASTWRITE,  st.st_mtime);
    WR32(data + FD_SIZE_HIGH, (uint32_t)((uint64_t)st.st_size >> 32));
    WR32(data + FD_SIZE_LOW,  (uint32_t)st.st_size);
    WR32(data + FD_RESERVED0, 0);
    WR32(data + FD_RESERVED1, 0);

    n = strlen(name);
    if (n > 259) {
        /* Truncating would hand back a name that opens nothing. Skipping it
           and saying so is the only answer that cannot be mistaken for a file
           that is there. */
        fprintf(stderr, "kernel32: FindFirstFile skipped \"%s\" -- %zu bytes "
                        "does not fit MAX_PATH-1 in WIN32_FIND_DATAA.\n",
                name, n);
        return 0;
    }
    memcpy((void *)(uintptr_t)(data + FD_FILENAME), name, n + 1);
    /* cAlternateFileName is the 8.3 short name. There is none here, and an
       empty string is what Windows itself gives on a volume without them. */
    WR8(data + FD_ALTNAME, 0);
    return 1;
}

/* Advance the handle to the next matching entry. 0 at end of directory. */
static int find_next(Handle *hh, uint32_t data)
{
    struct dirent *e;
    while ((e = readdir(hh->dir)) != NULL) {
        if (!win_match(hh->pattern, e->d_name)) continue;
        if (fd_fill(data, hh->dirpath, e->d_name)) return 1;
    }
    return 0;
}

void imp_KERNEL32_FindFirstFileA(CPU *C)
{
    const char *spec = ACS(0);
    uint32_t data = A(1), h;
    char win[1024], *slash;
    const char *pattern;
    const char *dirpath;
    Handle *hh;

    if (!spec || !data) {
        g_last_error = ERROR_FILE_NOT_FOUND;
        ret_std(C, INVALID_HANDLE, 2);
        return;
    }
    /* Split BEFORE translating: win_path() resolves case one component at a
       time against the filesystem, and the last component here is a pattern
       that matches no file, so resolving it would fail the whole path. */
    snprintf(win, sizeof win, "%s", spec);
    for (slash = win; *slash; slash++) if (*slash == '\\') *slash = '/';
    slash = strrchr(win, '/');
    if (slash) { *slash = 0; pattern = slash + 1; dirpath = win_path(win); }
    else       { pattern = win;                   dirpath = win_path("."); }

    h = h_alloc(H_FIND);
    hh = &g_h[h - 1];
    snprintf(hh->pattern, sizeof hh->pattern, "%s", pattern);
    snprintf(hh->dirpath, sizeof hh->dirpath, "%s", dirpath);
    hh->dir = opendir(hh->dirpath);
    if (!hh->dir) {
        fprintf(stderr, "kernel32: FindFirstFileA(\"%s\") -- \"%s\" is not a "
                        "directory this host can open (%s). Returning "
                        "ERROR_FILE_NOT_FOUND, which is what Windows would.\n",
                spec, hh->dirpath, strerror(errno));
        hh->kind = 0;
        g_last_error = ERROR_FILE_NOT_FOUND;
        ret_std(C, INVALID_HANDLE, 2);
        return;
    }
    if (!find_next(hh, data)) {
        closedir(hh->dir);
        hh->dir = NULL;
        hh->kind = 0;
        g_last_error = ERROR_FILE_NOT_FOUND;
        ret_std(C, INVALID_HANDLE, 2);
        return;
    }
    ret_std(C, h, 2);
}

void imp_KERNEL32_FindNextFileA(CPU *C)
{
    Handle *hh = h_get(A(0), H_FIND);
    uint32_t data = A(1);
    if (!data || !find_next(hh, data)) {
        g_last_error = ERROR_NO_MORE_FILES;
        ret_std(C, 0, 2);
        return;
    }
    ret_std(C, 1, 2);
}

void imp_KERNEL32_FindClose(CPU *C)
{
    Handle *hh = h_get(A(0), H_FIND);
    if (hh->dir) closedir(hh->dir);
    hh->dir = NULL;
    hh->kind = 0;
    ret_std(C, 1, 1);
}

void imp_KERNEL32_WideCharToMultiByte(CPU *C)
{
    /* The mirror of MultiByteToWideChar, and ASCII-only for the same reason. */
    uint32_t src = A(2); int32_t srclen = (int32_t)A(3);
    uint32_t dst = A(4); int32_t dstlen = (int32_t)A(5);
    int n = 0, i;
    const uint16_t *w = (const uint16_t *)(uintptr_t)src;
    if (srclen < 0) { while (w[n]) n++; n++; } else n = srclen;
    for (i = 0; i < n; i++)
        if (w[i] > 0x7F) {
            fprintf(stderr, "kernel32: WideCharToMultiByte got U+%04X; this "
                            "layer only narrows ASCII\n", w[i]);
            abort();
        }
    if (dstlen == 0) { ret_std(C, (uint32_t)n, 8); return; }
    if (n > dstlen) { ret_std(C, 0, 8); return; }
    for (i = 0; i < n; i++) WR8(dst + (uint32_t)i, (uint8_t)w[i]);
    ret_std(C, (uint32_t)n, 8);
}

/* ---- the Win32 heap ----------------------------------------------------
 *
 * All of it on the guest heap, because these hand out pointers the guest
 * stores. There is one heap, and GetProcessHeap returns a token rather than a
 * pointer so that a handle the guest invented is caught rather than followed.
 *
 * HEAP_ZERO_MEMORY is honoured. It would be easy to ignore -- most callers do
 * not set it -- and the failure from ignoring it is uninitialised memory that
 * happens to be zero in testing and is not in the field.
 */
#define PROCESS_HEAP_TOK  0x00020001u
#define HEAP_ZERO_MEMORY  0x00000008u

static uint32_t heap_check(uint32_t h)
{
    if (h != PROCESS_HEAP_TOK) {
        fprintf(stderr, "kernel32: heap handle 0x%08x is not the process heap; "
                        "this build has exactly one heap\n", h);
        abort();
    }
    return h;
}

void imp_KERNEL32_GetProcessHeap(CPU *C) { ret_std(C, PROCESS_HEAP_TOK, 0); }

void imp_KERNEL32_HeapAlloc(CPU *C)
{
    uint32_t flags = A(1), n = A(2), p;
    heap_check(A(0));
    p = guest_malloc(n);
    if (p && (flags & HEAP_ZERO_MEMORY)) memset((void *)(uintptr_t)p, 0, n);
    ret_std(C, p, 3);
}

void imp_KERNEL32_HeapFree(CPU *C)
{
    heap_check(A(0));
    guest_free(A(2));
    ret_std(C, 1, 3);
}

void imp_KERNEL32_LocalFree(CPU *C) { guest_free(A(0)); ret_std(C, 0, 1); }

/* ---- virtual memory ----------------------------------------------------
 *
 * Served from the guest heap rather than a real mmap: the guest stores these
 * pointers, so they must be 32-bit, and the arena already guarantees that.
 * The page-level semantics (reserve then commit separately, protection
 * changes) are NOT modelled -- if the game depends on them this will be
 * wrong, so MEM_RESERVE without MEM_COMMIT stops rather than pretending.
 */
#define MEM_COMMIT  0x1000u
#define MEM_RESERVE 0x2000u

/* How much memory this machine has, as far as the guest is concerned. Used by
   BOTH GlobalMemoryStatus and VirtualAlloc, because a budget the allocator
   does not enforce is not a budget -- the game allocates until allocation
   fails, and with the two disagreeing it took 937 MB after being told 512. */
static uint64_t phys_bytes(void)
{
    /* X2_PHYS_MB overrides it. How much memory to claim is an empirical
       question -- the game allocates until allocation fails, so the number
       decides how much it takes and possibly whether it gets far enough to
       finish initialising. A constant would have made that untestable. */
    static uint64_t v;
    if (!v) {
        const char *e = getenv("X2_PHYS_MB");
        unsigned mb = e && *e ? (unsigned)strtoul(e, NULL, 10) : 512u;
        if (mb < 64u) mb = 64u;
        v = (uint64_t)mb * 1048576ULL;
    }
    return v;
}
#define X2_PHYS_BYTES phys_bytes()

/* Memory layout is worth seeing: which arena the game placed where, and how
   big. X2_VERBOSE=1 prints it. Silent by default, because a shipping run
   should not narrate. */
static int verbose(void)
{
    static int v = -1;
    if (v < 0) { const char *e = getenv("X2_VERBOSE"); v = e && *e == '1'; }
    return v;
}

/*
 * What the guest has reserved, so a later commit over its OWN reservation can
 * be told from a request that collides with the runtime's memory.
 *
 * The first version returned success on any EEXIST, reasoning that a commit
 * over an existing reservation looks exactly like that. It does -- but so does
 * a request landing on the guest heap or a mapped module, and answering
 * "granted" there hands the game memory that belongs to us. That is the
 * plausible-value failure this layer refuses everywhere else, and it took
 * writing the runaway-allocation diagnostic to notice it.
 */
#define MAX_RESERVED 256
static struct { uint32_t base, size; } g_reserved[MAX_RESERVED];
static int g_nreserved;

static int guest_reserved(uint32_t base, uint32_t len)
{
    int i;
    for (i = 0; i < g_nreserved; i++)
        if (base >= g_reserved[i].base
            && base + len <= g_reserved[i].base + g_reserved[i].size)
            return 1;
    return 0;
}

/*
 * The span containing an address, for VirtualQuery.
 *
 * This table is the ONLY record that a guest reservation exists -- the address
 * is in no module and in no guest heap -- so a VirtualQuery that does not
 * consult it calls the guest's own memory FREE. That is not a cosmetic
 * inaccuracy: libIGCore's CRT grows its heap by scanning for a free region and
 * reserving it, so being told its own reservations are still free makes the
 * scan never finish. It reserved ~527 MB in 28 grows and only stopped when the
 * budget refused (C088). The allocator and the query have to describe the same
 * address space, which is the same defect as C070/C071 on the Xbox side.
 */
static int guest_reserved_span(uint32_t addr, uint32_t *base, uint32_t *size)
{
    int i;
    for (i = 0; i < g_nreserved; i++)
        if (addr >= g_reserved[i].base
            && addr - g_reserved[i].base < g_reserved[i].size) {
            *base = g_reserved[i].base;
            *size = g_reserved[i].size;
            return 1;
        }
    return 0;
}

/* Lowest reservation strictly above addr, or 0 -- so a FREE span stops at the
   next thing the guest owns instead of running through it. */
static uint32_t guest_reserved_next(uint32_t addr)
{
    uint32_t next = 0;
    int i;
    for (i = 0; i < g_nreserved; i++)
        if (g_reserved[i].base > addr && (!next || g_reserved[i].base < next))
            next = g_reserved[i].base;
    return next;
}

void imp_KERNEL32_VirtualAlloc(CPU *C)
{
    uint32_t addr = A(0), size = A(1), type = A(2), p;
    if (verbose())
        fprintf(stderr, "[mem] VirtualAlloc(0x%08x, %u = %.1f MB, type 0x%x)\n",
                addr, size, size / 1048576.0, type);
    if (addr) {
        /* A fixed address is directly implementable here in a way it is not in
           an emulator: guest addresses ARE host addresses, so the request can
           be passed to mmap as-is. The game asks for one to place its own
           heap. Rounded out to pages, because mmap requires it and Win32 does
           it silently. */
        uint32_t base = addr & ~0xFFFu;
        uint32_t len = ((addr - base) + size + 0xFFFu) & ~0xFFFu;
        void *got = mmap((void *)(uintptr_t)base, len, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE
                         | MAP_NORESERVE, -1, 0);
        if (got == MAP_FAILED || (uintptr_t)got != (uintptr_t)base) {
            if (got != MAP_FAILED) munmap(got, len);
            /* Already mapped: fine ONLY if the guest reserved it. Anything
               else is our own memory and must be refused. */
            if (errno == EEXIST && guest_reserved(base, len)) {
                ret_std(C, addr, 4);
                return;
            }
            if (errno == EEXIST)
                fprintf(stderr, "kernel32: VirtualAlloc(0x%08x, %u) collides "
                                "with memory the guest never reserved -- that "
                                "is the runtime's, and granting it would hand "
                                "the game our own heap or a mapped module\n",
                        base, size);
            else
                fprintf(stderr, "kernel32: VirtualAlloc could not place %u "
                                "bytes at 0x%08x: %s\n",
                        size, base, strerror(errno));
            g_last_error = 8u;                       /* ERROR_NOT_ENOUGH_MEMORY */
            ret_std(C, 0, 4);
            return;
        }
        if (g_nreserved < MAX_RESERVED) {
            g_reserved[g_nreserved].base = base;
            g_reserved[g_nreserved].size = len;
            g_nreserved++;
        }
        g_reserved_bytes += len;
        /* The game is told 512 MB exists (GlobalMemoryStatus). Reserving far
           past that is not a memory-pressure problem, it is a runaway loop,
           and saying so beats silently consuming the address space until
           something unrelated collides. */
        if (g_reserved_bytes > X2_PHYS_BYTES) {
            /* Refuse, do not abort. GlobalMemoryStatus told the guest how much
               memory exists; a game that allocates until allocation fails is
               doing the normal thing, and the two APIs have to agree or the
               budget it was given means nothing. Failing here is the honest
               answer to "is there more?" -- and it is what makes such a loop
               terminate. */
            if (verbose())
                fprintf(stderr, "[mem] refusing: %.0f MB reserved already, and "
                                "GlobalMemoryStatus reports %.0f MB of "
                                "physical memory\n",
                        g_reserved_bytes / 1048576.0,
                        X2_PHYS_BYTES / 1048576.0);
            munmap((void *)(uintptr_t)base, len);
            g_last_error = 8u;                       /* ERROR_NOT_ENOUGH_MEMORY */
            ret_std(C, 0, 4);
            return;
        }
        ret_std(C, addr, 4);
        return;
    }
    if (!(type & MEM_COMMIT)) {
        fprintf(stderr, "kernel32: VirtualAlloc with MEM_RESERVE but not "
                        "MEM_COMMIT needs real page semantics, which this "
                        "layer does not model\n");
        abort();
    }
    p = guest_malloc(size);
    if (p) memset((void *)(uintptr_t)p, 0, size);   /* VirtualAlloc zeroes */
    ret_std(C, p, 4);
}

/*
 * VirtualFree -- the counterpart of the mmap VirtualAlloc does, NOT a guest
 * heap free.
 *
 * It used to call guest_free(), which is wrong in a way that only showed up
 * once the game ran far enough to release memory: VirtualAlloc never allocates
 * from the guest heap, it mmaps, so every VirtualFree handed guest_free a
 * pointer it had never issued. guest_free caught it -- "free of a pointer
 * outside the guest heap" -- and that refusal is the only reason this was
 * found rather than corrupting the heap's free list.
 */
void imp_KERNEL32_VirtualFree(CPU *C)
{
    /* (lpAddress, dwSize, dwFreeType) */
    uint32_t addr = A(0), size = A(1), type = A(2);
    const uint32_t MEM_DECOMMIT = 0x4000u, MEM_RELEASE = 0x8000u;
    int i;
    if (verbose())
        fprintf(stderr, "[mem] VirtualFree(0x%08x, %u, type 0x%x)\n",
                addr, size, type);
    if (!addr) {
        /* Win32 fails this too, but quietly: freeing NULL is an ordinary no-op
           in cleanup code and does not deserve a report that reads like a
           defect. */
        g_last_error = 487u;
        ret_std(C, 0, 3);
        return;
    }
    if (type & MEM_RELEASE) {
        /* Win32 requires dwSize == 0 and releases the WHOLE reservation, so
           the size comes from the table rather than from the caller. */
        if (size != 0) {
            fprintf(stderr, "kernel32: VirtualFree(MEM_RELEASE) with size %u; "
                            "Win32 requires 0 and releases the whole "
                            "reservation\n", size);
            g_last_error = 87u;
            ret_std(C, 0, 3);
            return;
        }
        for (i = 0; i < g_nreserved; i++)
            if (g_reserved[i].base == addr) {
                munmap((void *)(uintptr_t)addr, g_reserved[i].size);
                g_reserved_bytes -= g_reserved[i].size;
                g_reserved[i] = g_reserved[--g_nreserved];
                ret_std(C, 1, 3);
                return;
            }
        fprintf(stderr, "kernel32: VirtualFree(MEM_RELEASE) of 0x%08x, which "
                        "this host never reserved -- refusing rather than "
                        "unmapping something it does not own\n", addr);
        g_last_error = 487u;                      /* ERROR_INVALID_ADDRESS */
        ret_std(C, 0, 3);
        return;
    }
    if (type & MEM_DECOMMIT) {
        /* Decommitted pages must fault on access; the reservation stays, so
           VirtualQuery still reports the range as the guest's. Leaving them
           readable would let a use-after-decommit read stale data silently. */
        uint32_t base = addr & ~0xFFFu;
        uint32_t len = ((addr - base) + size + 0xFFFu) & ~0xFFFu;
        if (len && mprotect((void *)(uintptr_t)base, len, PROT_NONE) != 0)
            fprintf(stderr, "kernel32: VirtualFree(MEM_DECOMMIT) could not "
                            "protect 0x%08x+%u: %s\n", base, len, strerror(errno));
        ret_std(C, 1, 3);
        return;
    }
    fprintf(stderr, "kernel32: VirtualFree(0x%08x) with type 0x%x, which is "
                    "neither MEM_DECOMMIT nor MEM_RELEASE\n", addr, type);
    g_last_error = 87u;
    ret_std(C, 0, 3);
}

void imp_KERNEL32_GlobalMemoryStatus(CPU *C)
{
    /* MEMORYSTATUS. Reported consistently: the game sizes caches from it, so
       the numbers have to agree with each other even though they are not the
       host's real figures. 512 MB total, half free. */
    uint32_t p = A(0);
    WR32(p +  0u, 32);                    /* dwLength */
    WR32(p +  4u, 50);                    /* dwMemoryLoad, percent */
    WR32(p +  8u, (uint32_t)X2_PHYS_BYTES);          /* dwTotalPhys */
    WR32(p + 12u, (uint32_t)(X2_PHYS_BYTES - g_reserved_bytes
                             > X2_PHYS_BYTES ? 0
                             : X2_PHYS_BYTES - g_reserved_bytes)); /* avail */
    WR32(p + 16u, (uint32_t)X2_PHYS_BYTES);          /* dwTotalPageFile */
    WR32(p + 20u, (uint32_t)(X2_PHYS_BYTES - g_reserved_bytes
                             > X2_PHYS_BYTES ? 0
                             : X2_PHYS_BYTES - g_reserved_bytes));
    WR32(p + 24u, 0x7FFF0000u);           /* dwTotalVirtual */
    WR32(p + 28u, 0x40000000u);           /* dwAvailVirtual */
    ret_std(C, 0, 1);
}

void imp_KERNEL32_VirtualQuery(CPU *C)
{
    /* MEMORY_BASIC_INFORMATION, for a guest address. The game uses it to ask
       "is this pointer valid and how big is the region", so the answer has to
       be about the GUEST address space, not the host's -- a host VirtualQuery
       equivalent would describe mappings the guest cannot see and would call
       our own runtime's memory "committed" to the guest.
       Only the fields the game reads are filled, and the rest are zeroed
       rather than left as whatever was in the buffer. */
    uint32_t addr = A(0), buf = A(1), len = A(2);
    X86Module *m;
    uint32_t base = 0, size = 0, state, protect;
    if (len < 28u) { ret_std(C, 0, 3); return; }
    m = x86_module_for(addr);
    if (m) { base = *m->base; size = m->size; state = 0x1000u; protect = 0x02u; }
    else if (guest_reserved_span(addr, &base, &size)) {
        /* Memory the guest itself reserved through VirtualAlloc. Reported as
           COMMIT rather than RESERVE because that is what we actually did:
           the reservation is mapped PROT_READ|PROT_WRITE, so it is readable
           and writable, and describing it as reserved-but-uncommitted would be
           the inaccuracy in the other direction. */
        state = 0x1000u; protect = 0x04u;
    }
    else {
        /* Not in a module. The guest heap and stacks are committed and
           writable; anything else is genuinely unmapped as far as the guest is
           concerned, and saying so is the useful answer. */
        extern int guest_heap_contains(uint32_t a, uint32_t *b, uint32_t *n);
        if (guest_heap_contains(addr, &base, &size)) { state = 0x1000u; protect = 0x04u; }
        else {
            /* FREE. The size must be the whole free span, not one page: a
               caller walking the address space advances by RegionSize, and
               reporting 4 KB turns a scan of a 4 GB space into a million
               iterations that never finish. Measured -- libIGCore's memory
               scan sat in exactly that loop. The span is the distance to the
               next thing the guest can see. */
            X86Module *k;
            uint32_t next = 0xFFFFF000u, hb, hn, rnext;
            for (k = x86_modules(); k; k = k->next)
                if (*k->base > addr && *k->base < next) next = *k->base;
            /* ...and at the next guest RESERVATION. Without this the free span
               is reported as running straight through memory the guest already
               owns, so a caller that trusts RegionSize reserves on top of
               itself. */
            rnext = guest_reserved_next(addr);
            if (rnext && rnext < next) next = rnext;
            if (guest_heap_contains(addr + 1u, &hb, &hn) == 0) {
                /* the heap starts somewhere above? find it the same way */
                if (guest_heap_contains(0, &hb, &hn) || 1) {
                    extern uint32_t guest_heap_base(void);
                    uint32_t gb = guest_heap_base();
                    if (gb > addr && gb < next) next = gb;
                }
            }
            base = addr & ~0xFFFu;
            size = next > base ? next - base : 0x1000u;
            state = 0x10000u;            /* MEM_FREE */
            protect = 0x01u;             /* PAGE_NOACCESS */
        }
    }
    if (verbose())
        fprintf(stderr, "[mem] VirtualQuery(0x%08x) -> base 0x%08x size %u "
                        "(%.1f MB) state %s\n", addr, base, size,
                size / 1048576.0,
                state == 0x10000u ? "FREE" : "COMMIT");
    memset((void *)(uintptr_t)buf, 0, 28);
    WR32(buf +  0u, base);               /* BaseAddress */
    WR32(buf +  4u, base);               /* AllocationBase */
    WR32(buf +  8u, protect);            /* AllocationProtect */
    WR32(buf + 12u, size);               /* RegionSize */
    WR32(buf + 16u, state);              /* State */
    WR32(buf + 20u, protect);            /* Protect */
    WR32(buf + 24u, 0x1000000u);         /* Type: MEM_IMAGE/PRIVATE */
    ret_std(C, 28, 3);
}
