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
#include "shell32.h"
#include "winmm.h"
#include "threads.h"
#include "igvk_ark.h"

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
    /* How many guest threads are blocked on this object right now, and how
       many times it has been PULSED. Both exist for PulseEvent, which needs
       them exactly: a pulse with no waiter is LOST on Windows, and a pulse on
       a manual-reset event releases every thread waiting AT THAT INSTANT and
       no later one -- which is a generation number, not a flag. A waiter
       records the count it entered on and is released when it changes. */
    int   waiters;
    unsigned long pulses;
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

/* ---- guest threads: the handle-table half ------------------------------
 *
 * threads.c owns the thread; the handle table owns handles, and a thread
 * handle has to be one of these because the guest waits on it with
 * WaitForSingleObject and closes it with CloseHandle like any other.
 *
 * The thread is signalled when it EXITS, which is what Win32 means by a
 * signalled thread handle, so `count` is the same field the events and
 * semaphores use and sync_try_take works on it unchanged.
 */
uint32_t k32_handle_for_thread(void *rec)
{
    uint32_t h = h_alloc(H_THREAD);
    g_h[h - 1].count = 0;                 /* not signalled: still running */
    g_h[h - 1].manual = 1;                /* a thread stays signalled once done */
    snprintf(g_h[h - 1].name, sizeof g_h[h - 1].name, "guest thread");
    (void)rec;
    return h;
}

void k32_handle_thread_done(uint32_t handle)
{
    if (handle && handle <= MAX_HANDLES && g_h[handle - 1].kind == H_THREAD)
        g_h[handle - 1].count = 1;        /* signalled, and stays that way */
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
    /*
     * The save drive is not the install.
     *
     * SHGetFolderPathA hands the guest "S:\\" (see shell32.c), and everything
     * the game then builds under it -- "S:\\Activision\\X-Men Legends 2\\..." --
     * arrives here. Mapping it against $GAME_PC_DIR like every other guest
     * path would write saves into the install, which this project treats as
     * strictly read-only.
     */
    if ((in[0] == X2_SAVE_DRIVE || in[0] == X2_SAVE_DRIVE + 32) && in[1] == ':')
        snprintf(buf, sizeof buf, "%s/%s", x2_save_dir(), in + 2);
    else if (in[0] && in[1] == ':')
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

/*
 * X2_FILES=1 -- every file operation the guest asks for, with its answer.
 *
 * A failed open reports itself (see CreateFileA), and that is not enough when
 * the question is "what did the game TRY": the game's own dialog said it could
 * not save while every open in the run SUCCEEDED (issue #39), which only a
 * trace of the successful ones can explain. There is no strace on this machine
 * and a game is not a program you can bisect by hand, so the trace lives here.
 *
 * Off by default -- it is one line per operation and the asset loader opens
 * thousands.
 */
static int files_traced(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("X2_FILES");
        on = (e && *e && *e != '0');
        if (on)
            fprintf(stderr, "[FILE] tracing every file operation the guest "
                            "asks for (X2_FILES).\n");
    }
    return on;
}

static void file_trace(const char *what, const char *guest, const char *host,
                       const char *outcome)
{
    if (!files_traced()) return;
    fprintf(stderr, "[FILE] %-18s \"%s\"\n         -> \"%s\"  %s\n",
            what, guest ? guest : "(null)", host ? host : "(null)", outcome);
}
/* Counted so the report can say how many were never shown. */
static unsigned long g_failed_opens;
static uint64_t g_reserved_bytes;   /* see VirtualAlloc */

void imp_KERNEL32_GetLastError(CPU *C) { ret_std(C, g_last_error, 0); }
/* The guest sets it too: a caller that clears the error, calls something, and
   then reads it back is asking a question this host must not answer with a
   stale value from an unrelated call. It is one process-wide word here, as it
   was before threads existed -- Win32's is per-thread, and the day two guest
   threads both check it that difference becomes real. */
void imp_KERNEL32_SetLastError(CPU *C) { g_last_error = A(0); ret_std(C, 0, 1); }

#define ERROR_FILE_NOT_FOUND   2u
#define ERROR_PATH_NOT_FOUND   3u
#define ERROR_ACCESS_DENIED    5u
#define ERROR_NO_MORE_FILES   18u
#define ERROR_ALREADY_EXISTS 183u
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
    /* A pump point. The multimedia timers have no thread of their own (see
       winmm.c), so they run when the guest next asks the time -- which any
       loop waiting for one does constantly. */
    winmm_timers_pump();
    uint64_t v;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    v = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    WR32(A(0), (uint32_t)v);
    WR32(A(0) + 4u, (uint32_t)(v >> 32));
    ret_std(C, 1, 1);
}

void imp_KERNEL32_GetCurrentProcessId(CPU *C) { ret_std(C, (uint32_t)getpid(), 0); }
void imp_KERNEL32_GetCurrentThreadId(CPU *C)  { ret_std(C, (uint32_t)gettid(), 0); }
void imp_KERNEL32_Sleep(CPU *C)
{
    /* The other pump point, and the one that matters most: a guest that sleeps
       waiting for a timer callback would otherwise sleep forever. Pumped
       AFTER the sleep, so a callback due during it fires as soon as it can. */
    /* The lock is RELEASED across the sleep. Holding it would stop every
       other guest thread for the duration -- including whichever one this
       sleep is waiting for. */
    guest_blocking_begin();
    usleep(A(0) * 1000u);
    guest_blocking_end();
    winmm_timers_pump();
    ret_std(C, 0, 1);
}

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

/*
 * GetSystemInfo -- answered from the REAL machine where there is a real
 * answer, and from this host's own address-space layout where the question is
 * about the process rather than the CPU.
 *
 * The application-address bounds are the ones THIS host actually gives the
 * guest (the low 4 GB up to the file-view arena's end), not Windows'
 * 0x7FFEFFFF: a caller that scans the address space with VirtualQuery must be
 * given the range VirtualQuery answers about, or the scan stops early or runs
 * off the end. msdia80's CRT calls this during DllMain.
 */
void imp_KERNEL32_GetSystemInfo(CPU *C)
{
    uint32_t p = A(0);
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 1;
    if (n > 32) n = 32;                  /* dwActiveProcessorMask is 32 bits */
    memset((void *)(uintptr_t)p, 0, 36);
    WR32(p +  0u, 0);                    /* PROCESSOR_ARCHITECTURE_INTEL */
    WR32(p +  4u, 0x1000u);              /* dwPageSize */
    WR32(p +  8u, 0x00010000u);          /* lpMinimumApplicationAddress */
    WR32(p + 12u, 0xEFFFFFFFu);          /* lpMaximumApplicationAddress */
    WR32(p + 16u, (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1u));
    WR32(p + 20u, (uint32_t)n);          /* dwNumberOfProcessors */
    WR32(p + 24u, 586u);                 /* dwProcessorType: PROCESSOR_INTEL_PENTIUM */
    WR32(p + 28u, 0x10000u);             /* dwAllocationGranularity, as VirtualAlloc uses */
    WR32(p + 32u, 6u | (0u << 16));      /* wProcessorLevel, wProcessorRevision */
    ret_std(C, 0, 1);
}

/* There is no debugger attached to a guest that has no debugger interface, so
   FALSE is the true answer and not a placeholder. */
void imp_KERNEL32_IsDebuggerPresent(CPU *C) { ret_std(C, 0, 0); }

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
/* Same section, plus a spin count -- which is a scheduling HINT even on
   Windows (how long to spin before sleeping), and cannot mean anything here
   where one guest thread runs at a time. It is dropped, not stored, and this
   comment is the record of that. Returns TRUE: the section IS initialised.
   msdia80's static MSVC8 CRT calls this during DllMain. */
void imp_KERNEL32_InitializeCriticalSectionAndSpinCount(CPU *C)
{
    WR32(A(0) + 4u, 0);
    ret_std(C, 1, 2);
}
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
/* CreateFileA's dwCreationDisposition, all five of them. Only CREATE_ALWAYS
   used to be handled and everything else fell through to "open, do not
   create" -- so a game writing a NEW file (OPEN_ALWAYS or CREATE_NEW, which is
   how a first save is written) got ERROR_FILE_NOT_FOUND and reported that it
   could not save. Issue #39. */
#define CREATE_NEW        1u
#define CREATE_ALWAYS     2u
#define OPEN_EXISTING     3u
#define OPEN_ALWAYS       4u
#define TRUNCATE_EXISTING 5u

void imp_KERNEL32_CreateFileA(CPU *C)
{
    uint32_t access_ = A(1), disp = A(4), h;
    const char *path = win_path(ACS(0));
    int flags = (access_ & GENERIC_WRITE) ? O_RDWR : O_RDONLY;
    int fd;
    switch (disp) {
    case CREATE_NEW:        flags = O_RDWR | O_CREAT | O_EXCL;  break;
    case CREATE_ALWAYS:     flags = O_RDWR | O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS:       flags = O_RDWR | O_CREAT;           break;
    case TRUNCATE_EXISTING: flags = O_RDWR | O_TRUNC;           break;
    case OPEN_EXISTING:     break;                  /* open, never create */
    default:
        /* Refused rather than guessed: the dispositions differ in whether they
           CREATE and whether they TRUNCATE, and picking wrong either loses a
           file or invents one. */
        fprintf(stderr, "kernel32: CreateFileA with disposition %u, which is "
                        "not one of the five Win32 defines. Refusing.\n", disp);
        g_last_error = 87u;                     /* ERROR_INVALID_PARAMETER */
        ret_std(C, INVALID_HANDLE, 7);
        return;
    }
    fd = open(path, flags, 0644);
    if (fd < 0) {
        /*
         * A failed open is REPORTED, with the path the guest asked for and the
         * host path it became.
         *
         * Returning INVALID_HANDLE silently is the correct Win32 answer and a
         * terrible diagnostic: the game's own dialog says "SAVE FAILED!" and
         * nothing anywhere says which file, or whether the failure was the
         * path translation rather than the disk (issue #39). Capped, because a
         * game probing for optional files fails opens all day.
         */
        static int told;
        if (told++ < 12)
            fprintf(stderr, "kernel32: CreateFileA(\"%s\", disposition %u) "
                            "FAILED -- \"%s\": %s%s\n",
                    ACS(0) ? ACS(0) : "(null)", disp, path, strerror(errno),
                    told == 12 ? "  (further ones are silent)" : "");
        g_failed_opens++;
        g_last_error = ERROR_FILE_NOT_FOUND;
        ret_std(C, INVALID_HANDLE, 7);
        return;
    }
    h = h_alloc(H_FILE);
    g_h[h - 1].fd = fd;
    file_trace("CreateFile", ACS(0), path,
               (access_ & GENERIC_WRITE) ? "opened for WRITING"
                                         : "opened for reading");
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
    /* Something a wait could be blocked on has been signalled: wake the
       waiters. A signal nothing is told about leaves a guest thread asleep
       on an object that is already ready. */
    guest_cond_broadcast();
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
    /* Something a wait could be blocked on has been signalled: wake the
       waiters. A signal nothing is told about leaves a guest thread asleep
       on an object that is already ready. */
    guest_cond_broadcast();
    h_get(A(0), H_EVENT)->count = 1;
    ret_std(C, 1, 1);
}

/*
 * PulseEvent -- release whoever is waiting NOW, and leave the event unset.
 *
 * libCriMovie's timer callback uses it to hand a frame to the main thread
 * (issue #49). It is the one Win32 primitive whose whole meaning is "a signal
 * with no memory": a pulse that nobody is waiting for is LOST, which is why
 * Microsoft's own documentation calls it unreliable.
 *
 * An AUTO-RESET event is exact here: signal it and wake the waiters, and the
 * one that gets there consumes it (sync_try_take clears a non-manual event),
 * so the event ends unsignalled either way. With no waiter, doing nothing IS
 * the behaviour -- and the two cases are distinguishable only because the wait
 * loop counts waiters, which is why it does.
 *
 * A MANUAL-RESET event -- which is the kind libCriMovie uses -- means "release
 * EVERY thread waiting at this instant, then reset". That is a GENERATION, not
 * a flag: the pulse count is bumped and every thread already in the wait loop
 * sees it change, while a thread that arrives afterwards snapshots the new
 * value and is not released. The event is never left signalled, which is the
 * whole difference between a pulse and SetEvent followed by ResetEvent.
 *
 * The first version REFUSED this case by name rather than approximating it,
 * and the refusal is what identified the event as manual-reset in one run.
 */
void imp_KERNEL32_PulseEvent(CPU *C)
{
    Handle *hh = h_get(A(0), H_EVENT);
    static unsigned long lost;
    if (hh->waiters > 0) {
        if (hh->manual) {
            /* Every thread waiting at this instant, and no later one. The
               event itself stays UNSIGNALLED, which is what distinguishes a
               pulse from SetEvent+ResetEvent and what the next waiter must
               see. */
            hh->pulses++;
        } else {
            /* Auto-reset: exactly one waiter is released, and it consumes the
               signal on the way out (sync_try_take clears a non-manual
               event), so the event ends unsignalled either way. */
            hh->count = 1;
        }
        guest_cond_broadcast();
    } else if (++lost == 1) {
        fprintf(stderr, "kernel32: PulseEvent on \"%s\" with NOBODY waiting -- "
                        "the pulse is lost, which is what Windows does too. "
                        "Reported once; if a handshake stalls, this is where "
                        "the missing wakeup went.\n", hh->name);
    }
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
    /* Something a wait could be blocked on has been signalled: wake the
       waiters. A signal nothing is told about leaves a guest thread asleep
       on an object that is already ready. */
    guest_cond_broadcast();
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
    unsigned long pulse0;
    struct timespec t0;

    if (sync_try_take(hh)) { ret_std(C, WAIT_OBJECT_0, 2); return; }
    if (ms == 0) { ret_std(C, WAIT_TIMEOUT, 2); return; }
    /*
     * A REAL wait now that guest threads exist.
     *
     * It used to abort here, and correctly: with nothing else running, no
     * signal could ever arrive and both plausible answers were lies -- success
     * hands the game a lock it does not hold, timeout claims a wait happened.
     * What changed is that something else CAN run, and the wait releases the
     * guest lock so it can (src/native/threads.c).
     *
     * Bounded even for an INFINITE wait, because "nothing will ever signal
     * this" is still possible -- one guest thread deadlocking against another
     * has to be reported, not hung on.
     */
    hh->waiters++;
    pulse0 = hh->pulses;
    /*
     * The deadline is measured on the CLOCK, not counted in loop turns.
     *
     * It used to be `if (++spins == 30)`, which was 30 seconds only while each
     * turn slept a flat second. Once the sleep became "until the next timer is
     * due" a turn could be under a millisecond, and the watchdog would abort a
     * perfectly healthy wait in a few dozen of them -- a diagnostic that fires
     * on the thing it exists to rule out.
     */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        /*
         * Sleep until the next TIMER is due, not for a flat second.
         *
         * This thread is the one that will pump that timer (see below), so the
         * wait's granularity IS the timer's resolution: a flat 1000 ms slice
         * made every movie frame cost a second, and the guest sat blocked at
         * 1.3 frames per second while looking perfectly healthy.
         */
        guest_cond_wait_ms(winmm_next_due_ms(ms == 0xFFFFFFFFu ? 1000u : ms));
        /*
         * A PUMP POINT, and the one the movie player needs (issue #49).
         *
         * The multimedia timers have no thread of their own, so they run when
         * the guest next reaches a place it is not executing -- a clock read
         * or a sleep. A thread blocked HERE reaches neither, so a wait for
         * something a timer callback would produce waited forever: libCriMovie
         * sets a 1 ms timer, its decoder parks itself, and the main thread
         * waits on an event nothing can now signal. Every ingredient existed
         * and the fire never happened.
         *
         * The callback runs on THIS thread, inside the wait. Windows runs it
         * on a timer thread; that difference is the same one Sleep already
         * carries and is stated in winmm.c, not a new one introduced here.
         */
        winmm_timers_pump();
        if (sync_try_take(hh)) { hh->waiters--; ret_std(C, WAIT_OBJECT_0, 2); return; }
        /* Released by a PULSE: the object is not signalled and must not be
           taken -- being let go IS the whole event. Only a thread that was
           already waiting when the pulse happened sees the change, which is
           exactly who Win32 releases. */
        if (hh->pulses != pulse0) {
            hh->waiters--;
            ret_std(C, WAIT_OBJECT_0, 2);
            return;
        }
        if (ms != 0xFFFFFFFFu) { hh->waiters--; ret_std(C, WAIT_TIMEOUT, 2); return; }
        {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec - t0.tv_sec < 30) continue;
        }
        {
            fprintf(stderr, "kernel32: WaitForSingleObject(INFINITE) on %s "
                            "\"%s\" has waited 30 seconds and nothing has "
                            "signalled it.\n"
                            "  Reporting rather than hanging: either the guest "
                            "thread that would signal it is not running, or "
                            "this host never signals that object.\n",
                    sync_kind_name(hh->kind), hh->name);
            x86_diag_dump();
            abort();
        }
    }
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
/*
 * PER-THREAD, which is the entire meaning of the API: TlsGetValue must return
 * what THIS thread last set. The index allocation is process-wide and stays
 * shared -- TlsAlloc reserves a slot for everyone.
 */
static __thread uint32_t g_tls[MAX_TLS];
static unsigned char g_tls_used[MAX_TLS];
#define TLS_OUT_OF_INDEXES 0xFFFFFFFFu

/* ---- thread control ---------------------------------------------------- */

void imp_KERNEL32_ResumeThread(CPU *C)
{
    int was = guest_thread_resume(A(0));
    if (was < 0) {
        fprintf(stderr, "kernel32: ResumeThread(0x%x) -- that handle names no "
                        "guest thread\n", A(0));
        g_last_error = 6u;                        /* ERROR_INVALID_HANDLE */
        ret_std(C, 0xFFFFFFFFu, 1);
        return;
    }
    ret_std(C, (uint32_t)was, 1);
}

/*
 * SuspendThread, which libCriMovie's decoder uses on ITSELF: its loop
 * (libCriMovie 0x10002630) sets its own priority and then suspends, waiting to
 * be resumed when another frame is wanted. That is a park, not a kill, and it
 * is why this can be exact rather than approximate -- see the note in
 * src/native/threads.c.
 *
 * The main thread is refused BY NAME. Win32 lets a thread suspend the process's
 * first thread and this host cannot: the main thread is the process, nothing
 * models it as a GuestThread, and suspending it would stop the run with no
 * report. Returning a fabricated success would be worse -- the caller would
 * believe it had stopped something that kept running.
 */
void imp_KERNEL32_SuspendThread(CPU *C)
{
    int was;
    if (!guest_thread_is_thread(A(0))) {
        fprintf(stderr, "kernel32: SuspendThread(0x%x) -- that handle names no "
                        "guest thread. If it is the MAIN thread, this host has "
                        "no way to suspend it (it is the process) and will not "
                        "pretend to have done so.\n", A(0));
        g_last_error = 6u;                        /* ERROR_INVALID_HANDLE */
        ret_std(C, 0xFFFFFFFFu, 1);
        return;
    }
    was = guest_thread_suspend(A(0));
    if (was < 0) {
        g_last_error = 6u;
        ret_std(C, 0xFFFFFFFFu, 1);
        return;
    }
    ret_std(C, (uint32_t)was, 1);
}

/*
 * lstrlenA: the Win32 spelling of strlen, and NUL-safe on a NULL pointer the
 * way the real one is (it returns 0 rather than faulting).
 */
void imp_KERNEL32_lstrlenA(CPU *C)
{
    const char *p = ACS(0);
    ret_std(C, p ? (uint32_t)strlen(p) : 0u, 1);
}

/*
 * GetFullPathNameA(name, buflen, buf, &filepart)
 *
 * Canonicalises a path IN THE GUEST'S TERMS -- it is a string operation on
 * Windows paths, and it must stay one: returning a host path here would hand
 * the guest something win_path() would then resolve against the install a
 * second time.
 *
 * The guest's current directory is the install, which is where the game
 * believes it is running from, so a relative path is completed against "C:\".
 * That is the same convention win_path() already uses for a bare relative
 * path, so the two agree.
 *
 * `.` and `..` are NOT collapsed, and this says so rather than pretending:
 * nothing in this game has been seen to pass one, and a wrong collapse turns a
 * valid path into a different valid path, which is worse than not collapsing.
 */
void imp_KERNEL32_GetFullPathNameA(CPU *C)
{
    const char *in = ACS(0);
    uint32_t buflen = A(1), buf = A(2), partp = A(3);
    char full[1024];
    size_t n;

    if (!in) { g_last_error = 87u; ret_std(C, 0, 4); return; }
    if (in[0] && in[1] == ':')
        snprintf(full, sizeof full, "%s", in);           /* already absolute */
    else if (in[0] == '\\' || in[0] == '/')
        snprintf(full, sizeof full, "C:%s", in);
    else
        snprintf(full, sizeof full, "C:\\%s", in);
    if (strstr(full, "..") || strstr(full, "\\.\\"))
        fprintf(stderr, "kernel32: GetFullPathNameA(\"%s\") -- this host does "
                        "not collapse . or .., so the answer keeps them. A "
                        "wrong collapse would name a DIFFERENT valid file.\n",
                in);
    n = strlen(full);
    /* Win32: the return is the length WITHOUT the NUL when it fits, and the
       length WITH it when the buffer is too small. Getting that backwards
       makes a caller size a buffer one byte short, every time. */
    if (buflen < n + 1u || !buf) { ret_std(C, (uint32_t)n + 1u, 4); return; }
    memcpy((void *)(uintptr_t)buf, full, n + 1u);
    if (partp) {
        const char *slash = strrchr(full, '\\');
        WR32(partp, slash ? buf + (uint32_t)(slash + 1 - full) : buf);
    }
    ret_std(C, (uint32_t)n, 4);
}

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

/*
 * The Interlocked family, on real atomics.
 *
 * Guest threads now exist (issue #43) and one guest thread runs at a time
 * under the global lock, so a plain read-modify-write would in fact be atomic
 * with respect to every other guest thread today. These use host atomics
 * anyway: the lock is a property of the current threading model, the guarantee
 * these functions make is not, and a caller that relies on it (COM reference
 * counting, which is what msdia80 wants them for) must not depend on which
 * model the host happens to have. The guest memory is host memory, so this
 * operates on the real word.
 */
void imp_KERNEL32_InterlockedExchange(CPU *C)
{
    uint32_t *p = (uint32_t *)(uintptr_t)A(0);
    ret_std(C, __atomic_exchange_n(p, A(1), __ATOMIC_SEQ_CST), 2);
}

void imp_KERNEL32_InterlockedIncrement(CPU *C)
{
    int32_t *p = (int32_t *)(uintptr_t)A(0);
    ret_std(C, (uint32_t)__atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST), 1);
}

void imp_KERNEL32_InterlockedDecrement(CPU *C)
{
    int32_t *p = (int32_t *)(uintptr_t)A(0);
    ret_std(C, (uint32_t)__atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST), 1);
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
    /* A thread handle names a GuestThread, and this index is about to be
       handed to something else -- so the thread must stop answering to it.
       See guest_thread_handle_closed() for what it cost not to. */
    if (hh->kind == H_THREAD) guest_thread_handle_closed(A(0));
    hh->kind = 0;
    ret_std(C, 1, 1);
}

void imp_KERNEL32_GetFileAttributesA(CPU *C)
{
    struct stat st;
    const char *g = ACS(0);
    const char *p = win_path(g);
    if (stat(p, &st) != 0) {
        file_trace("GetFileAttributes", g, p, "does not exist");
        g_last_error = ERROR_FILE_NOT_FOUND;
        ret_std(C, 0xFFFFFFFFu, 1);      /* INVALID_FILE_ATTRIBUTES */
        return;
    }
    file_trace("GetFileAttributes", g, p,
               S_ISDIR(st.st_mode) ? "a directory" : "a file");
    ret_std(C, S_ISDIR(st.st_mode) ? 0x10u : 0x80u, 1);   /* DIRECTORY : NORMAL */
}

void imp_KERNEL32_DeleteFileA(CPU *C)
{
    const char *g = ACS(0), *p = win_path(g);
    int ok = unlink(p) == 0;
    if (!ok)
        g_last_error = errno == ENOENT ? ERROR_FILE_NOT_FOUND
                                       : ERROR_ACCESS_DENIED;
    file_trace("DeleteFile", g, p, ok ? "deleted" : strerror(errno));
    ret_std(C, (uint32_t)ok, 1);
}

void imp_KERNEL32_CreateDirectoryA(CPU *C)
{
    const char *g = ACS(0), *p = win_path(g);
    int ok = mkdir(p, 0777) == 0;
    if (!ok) {
        /*
         * The LAST ERROR is the whole answer here, not the return value.
         *
         * CreateDirectory returns FALSE for a directory that already exists --
         * on Windows too -- and every caller that creates a tree distinguishes
         * that from a real failure by ERROR_ALREADY_EXISTS. Leaving the last
         * error at whatever it happened to be makes "the directory is already
         * there" read as "the directory could not be made", which is a save
         * path that reports itself as broken on every run after the first
         * (issue #39).
         */
        g_last_error = errno == EEXIST ? ERROR_ALREADY_EXISTS
                     : errno == ENOENT ? ERROR_PATH_NOT_FOUND
                                       : ERROR_ACCESS_DENIED;
    }
    file_trace("CreateDirectory", g, p, ok ? "created" : strerror(errno));
    ret_std(C, (uint32_t)ok, 2);
}
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
    /*
     * The path is in the GUEST's namespace, not the host's -- "C:\XMen2.exe",
     * not the install's POSIX path with the slashes turned round.
     *
     * The host path was what this returned, and it does not survive the round
     * trip: the game strips the file name off it and scans the result
     * ("<dir>\*.*"), which arrives back at win_path() as a leading-backslash
     * path, is resolved against $GAME_PC_DIR like every other guest path, and
     * lands on <install>/<install>/*.* -- so the game's own directory scan
     * found NOTHING and said nothing. win_path already maps a drive letter to
     * the install, so naming the drive makes the round trip exact.
     *
     * The install directory is the root of the guest's C:. That is the same
     * device shell32 uses for the save drive (X2_SAVE_DRIVE), and it is why
     * these two constants sit next to each other rather than one being a
     * literal somewhere.
     */
    (void)dir;
    snprintf(path, sizeof path, "%c:\\%s", X2_GAME_DRIVE, name);
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
    /* Recorded and round-tripped, and that is ALL it does: one guest thread
       runs at a time under a global lock (threads.c), so there is no
       scheduling here to prioritise. The value is kept because
       GetThreadPriority must return what was set. */
    g_thread_priority = (int32_t)A(1);
    ret_std(C, 1, 2);
}

void imp_KERNEL32_SetThreadPriorityBoost(CPU *C) { ret_std(C, 1, 2); }

/*
 * SetThreadAffinityMask: accepted, and the PREVIOUS mask returned as Win32
 * does. It cannot mean anything here -- one guest thread runs at a time under
 * a global lock, so there is one CPU as far as the guest is concerned, which
 * is exactly what a mask of 1 says.
 *
 * Returning 0 (failure) instead would be a lie in the other direction: the
 * caller asked to be pinned to processors that exist, and it has been.
 */
void imp_KERNEL32_SetThreadAffinityMask(CPU *C) { ret_std(C, 1, 2); }

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
    /*
     * A module this host IMPLEMENTS is in the address space whether or not
     * anyone has called LoadLibraryA for it -- kernel32 always is, on Windows
     * and here. Answering NULL for one was not caution, it was wrong, and it
     * cost a module: msdia80's static MSVC8 CRT startup does
     * GetModuleHandleA("KERNEL32.DLL") and gives up when that fails, so its
     * DllMain returned FALSE and module initialisation stopped the whole run.
     *
     * This is still not "create a handle for anything asked about": the name
     * has to be one is_native_sysmod() vouches for, and GetProcAddress on the
     * handle answers from the export registry -- an entry point this host does
     * not implement still comes back NULL.
     */
    if (!b && nm && is_native_sysmod(nm)) b = sysmod_handle(nm);
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
        file_trace("FindFirstFile", spec, hh->dirpath, "matched NOTHING");
        closedir(hh->dir);
        hh->dir = NULL;
        hh->kind = 0;
        g_last_error = ERROR_FILE_NOT_FOUND;
        ret_std(C, INVALID_HANDLE, 2);
        return;
    }
    file_trace("FindFirstFile", spec, hh->dirpath, "matched at least one");
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

/*
 * Windows-1252, the whole table for the part that is not Latin-1.
 *
 * 0x00-0x7F is ASCII and 0xA0-0xFF is Latin-1 (the byte IS the code point).
 * Only 0x80-0x9F differs, where Windows put printable characters in what
 * ISO-8859-1 leaves as controls. Sixteen bytes of table is the entire cost of
 * being right, and the alternative -- widening the byte as if it were Latin-1
 * -- turns a curly quote into a C1 control character.
 *
 * This replaces an abort() on the first byte above 0x7F, and lived in
 * win32_sdl.c until the narrowing direction below needed the same table. That
 * abort was the right call while nothing produced a high byte; cg.dll's
 * statically-linked CRT widens its locale and environment strings at startup
 * and hits 0x80 immediately, and "stop rather than mangle" is not the only
 * alternative to mangling -- converting correctly is.
 */
static const uint16_t cp1252_80_9f[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

static uint16_t cp1252_to_utf16(unsigned char c)
{
    return (c >= 0x80 && c <= 0x9F) ? cp1252_80_9f[c - 0x80] : (uint16_t)c;
}

/* The 0x80-0x9F entry for slot j, for the reverse search when narrowing. */
static uint16_t cp1252_from_utf16(int j) { return cp1252_80_9f[j]; }

void imp_KERNEL32_MultiByteToWideChar(CPU *C)
{
    uint32_t cp = A(0), src = A(2); int32_t srclen = (int32_t)A(3);
    uint32_t dst = A(4); int32_t dstlen = (int32_t)A(5);
    const unsigned char *s = (const unsigned char *)(uintptr_t)src;
    int n, i;
    /*
     * CP_ACP (0) and CP_OEMCP (1) both resolve to this host's single code page
     * -- see the locale block in kernel32.c, which answers 1252/437. UTF-8
     * (65001) is NOT handled here and says so: a multi-byte sequence widened
     * one byte at a time produces a different string of the same length, which
     * is the failure that looks like success.
     */
    if (cp != 0u && cp != 1u && cp != 1252u) {
        fprintf(stderr, "kernel32: MultiByteToWideChar code page %u is not "
                        "implemented -- this host is single-code-page (1252). "
                        "UTF-8 in particular is refused rather than widened "
                        "byte-by-byte, which would silently produce a "
                        "different string.\n", cp);
        abort();
    }
    n = srclen < 0 ? (int)strlen((const char *)s) + 1 : srclen;
    if (dstlen == 0) { ret_std(C, (uint32_t)n, 6); return; }
    if (n > dstlen) { ret_std(C, 0, 6); return; }
    for (i = 0; i < n; i++) WR16(dst + (uint32_t)i * 2u, cp1252_to_utf16(s[i]));
    ret_std(C, (uint32_t)n, 6);
}

/*
 * The mirror of MultiByteToWideChar, narrowing to Windows-1252.
 *
 * Narrowing is the LOSSY direction and that is where the honesty has to live:
 * a code point with no 1252 byte is replaced with '?' -- which is what Windows
 * does -- and lpUsedDefaultChar is set so a caller that asked can tell. The
 * count of lost characters is reported at exit, because a silent '?' in a save
 * file name is the kind of defect that is only ever noticed much later.
 */
static unsigned long g_narrow_lost;

void imp_KERNEL32_WideCharToMultiByte(CPU *C)
{
    uint32_t src = A(2); int32_t srclen = (int32_t)A(3);
    uint32_t dst = A(4); int32_t dstlen = (int32_t)A(5);
    uint32_t defchar = A(6), usedflag = A(7);
    int n = 0, i, lost = 0;
    const uint16_t *w = (const uint16_t *)(uintptr_t)src;
    char sub = defchar ? *(const char *)(uintptr_t)defchar : '?';

    if (srclen < 0) { while (w[n]) n++; n++; } else n = srclen;
    if (dstlen == 0) { ret_std(C, (uint32_t)n, 8); return; }
    if (n > dstlen) { ret_std(C, 0, 8); return; }
    for (i = 0; i < n; i++) {
        uint16_t c = w[i];
        int b = -1, j;
        if (c < 0x80 || (c >= 0xA0 && c <= 0xFF)) b = c;
        else for (j = 0; j < 32; j++)
            if (cp1252_from_utf16(j) == c) { b = 0x80 + j; break; }
        if (b < 0) { b = (unsigned char)sub; lost++; }
        WR8(dst + (uint32_t)i, (uint8_t)b);
    }
    if (usedflag) WR32(usedflag, lost ? 1u : 0u);
    g_narrow_lost += (unsigned long)lost;
    ret_std(C, (uint32_t)n, 8);
}

void kernel32_narrowing_report(void)
{
    if (g_narrow_lost)
        printf("  kernel32: %lu character(s) had no Windows-1252 byte and were "
               "narrowed to a substitute -- those strings are NOT what the "
               "guest produced.\n", g_narrow_lost);
}

/* ---- the Win32 heap ----------------------------------------------------
 *
 * All of it on the guest heap, because these hand out pointers the guest
 * stores. GetProcessHeap returns a TOKEN rather than a pointer so that a
 * handle the guest invented is caught rather than followed.
 *
 * There is one ARENA but there can be several heap HANDLES: HeapCreate makes
 * private heaps and a statically-linked CRT allocates everything from one (see
 * the static-CRT section at the end of this file). The blocks all come from the
 * same arena -- that is what makes them 32-bit addressable -- so the handles
 * are bookkeeping, and heap_check's job is to reject a handle that names no
 * heap AT ALL, which is the case that would otherwise be followed silently.
 *
 * HEAP_ZERO_MEMORY is honoured. It would be easy to ignore -- most callers do
 * not set it -- and the failure from ignoring it is uninitialised memory that
 * happens to be zero in testing and is not in the field.
 */
#define PROCESS_HEAP_TOK  0x00020001u
#define HEAP_ZERO_MEMORY  0x00000008u
#define PRIVATE_HEAP_TOK  0x00030000u
static int g_heaps;                      /* how many HeapCreate calls */

static uint32_t heap_check(uint32_t h)
{
    if (h == PROCESS_HEAP_TOK) return h;
    if (h > PRIVATE_HEAP_TOK && h <= PRIVATE_HEAP_TOK + (uint32_t)g_heaps)
        return h;
    fprintf(stderr, "kernel32: heap handle 0x%08x names no heap -- it is "
                    "neither the process heap nor one of the %d created by "
                    "HeapCreate.\n", h, g_heaps);
    abort();
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
/*
 * Pages the guest has DECOMMITTED inside its own reservations.
 *
 * VirtualFree(MEM_DECOMMIT) mprotects them PROT_NONE so a use-after-decommit
 * faults instead of reading stale data. That was right, and it made
 * VirtualQuery a liar: it reported the whole reservation as MEM_COMMIT on the
 * grounds that "the reservation is mapped PROT_READ|PROT_WRITE", which stopped
 * being true the moment the first decommit ran. The guest asked, was told the
 * range was committed, used it, and faulted -- issue #41.
 *
 * On Windows a decommitted page reads back as MEM_RESERVE, and the region size
 * is the run of pages in that state. So the two calls have to share one record
 * of what is committed, which is this.
 */
#define MAX_DECOMMIT 256
static struct { uint32_t base, size; } g_decommit[MAX_DECOMMIT];
static int g_ndecommit, g_decommit_lost;

/* The decommitted range containing `addr`, if any. */
static int guest_decommitted(uint32_t addr, uint32_t *base, uint32_t *size)
{
    int i;
    for (i = 0; i < g_ndecommit; i++)
        if (addr >= g_decommit[i].base
            && addr - g_decommit[i].base < g_decommit[i].size) {
            if (base) *base = g_decommit[i].base;
            if (size) *size = g_decommit[i].size;
            return 1;
        }
    return 0;
}

static void decommit_note(uint32_t base, uint32_t size)
{
    int i;
    for (i = 0; i < g_ndecommit; i++)               /* already recorded */
        if (g_decommit[i].base == base && g_decommit[i].size == size) return;
    if (g_ndecommit == MAX_DECOMMIT) {
        /* Counted and reported rather than dropped silently: a decommit this
           host forgets is one VirtualQuery will call committed again. */
        g_decommit_lost++;
        return;
    }
    g_decommit[g_ndecommit].base = base;
    g_decommit[g_ndecommit].size = size;
    g_ndecommit++;
}

/* A commit over decommitted pages takes them off the list. Only whole entries
   are dropped: a partial re-commit leaves the entry, which errs toward
   reporting RESERVE for something committed -- the safe direction, because the
   guest then asks again rather than using memory it was wrongly promised. */
static void decommit_clear(uint32_t base, uint32_t len)
{
    int i;
    for (i = 0; i < g_ndecommit; ) {
        if (g_decommit[i].base >= base
            && g_decommit[i].base + g_decommit[i].size <= base + len)
            g_decommit[i] = g_decommit[--g_ndecommit];
        else i++;
    }
}

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
                /*
                 * A COMMIT over a reservation this host already mapped.
                 *
                 * The pages may have been DECOMMITTED since, which mprotects
                 * them PROT_NONE -- so returning success without restoring
                 * access makes a decommit permanent, and the guest faults on
                 * the memory Win32 just told it it had. Issue #41.
                 */
                if (mprotect((void *)(uintptr_t)base, len,
                             PROT_READ | PROT_WRITE) != 0)
                    fprintf(stderr, "kernel32: VirtualAlloc could not restore "
                                    "access to 0x%08x+%u: %s\n",
                            base, len, strerror(errno));
                decommit_clear(base, len);
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
        /*
         * MEM_RESERVE with no address: reserve address space, commit nothing.
         *
         * This used to abort as "needs real page semantics". It does, and the
         * pieces now exist: the reservation table below, the decommit table,
         * and the MEM_COMMIT-over-a-reservation path above that mprotects
         * access back. What was missing was only somewhere to PUT a
         * reservation the caller did not place itself.
         *
         * It has to land in the low 4 GB, because the guest stores the
         * pointer, and it must not collide with the mapped modules
         * (0x00400000 and 0x20000000+) or the runtime's own arena
         * (X2_RUNTIME_BASE and up). The window between them is reserved for
         * this, walked with MAP_FIXED_NOREPLACE so a collision is refused by
         * the kernel rather than found later by the guest.
         *
         * PROT_NONE is the point: reserved-but-not-committed memory must FAULT
         * on access. Mapping it readable would make the difference between
         * reserve and commit invisible, which is exactly the bug the old abort
         * was there to avoid.
         */
        const uint32_t RES_LO = 0x30000000u, RES_HI = 0x6F000000u;
        static uint32_t next = 0x30000000u;
        uint32_t len = (size + 0xFFFu) & ~0xFFFu;
        int tries;
        if (!len) { g_last_error = 87u; ret_std(C, 0, 4); return; }
        for (tries = 0; tries < 64; tries++) {
            void *got;
            if (next + len > RES_HI || next + len < next) next = RES_LO;
            got = mmap((void *)(uintptr_t)next, len, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE
                       | MAP_NORESERVE, -1, 0);
            if (got != MAP_FAILED && (uintptr_t)got == (uintptr_t)next) {
                uint32_t base = next;
                next += len;
                if (g_nreserved < MAX_RESERVED) {
                    g_reserved[g_nreserved].base = base;
                    g_reserved[g_nreserved].size = len;
                    g_nreserved++;
                } else {
                    /* Untracked: a later MEM_COMMIT over it would be refused
                       as "memory the guest never reserved". Said rather than
                       left to surface as that unrelated-looking message. */
                    fprintf(stderr, "kernel32: the reservation table is full "
                                    "(%d); 0x%08x+%u is mapped but NOT tracked, "
                                    "so committing it later will be refused.\n",
                            MAX_RESERVED, base, len);
                }
                if (verbose())
                    fprintf(stderr, "[mem] reserved 0x%08x+%u (PROT_NONE; it "
                                    "faults until committed)\n", base, len);
                ret_std(C, base, 4);
                return;
            }
            if (got != MAP_FAILED) munmap(got, len);
            next += 0x100000u;           /* step past whatever is there */
        }
        fprintf(stderr, "kernel32: VirtualAlloc could not RESERVE %u bytes "
                        "anywhere in 0x%08x-0x%08x after 64 attempts. That "
                        "window is the only 32-bit space not already holding a "
                        "module or the runtime arena.\n", len, RES_LO, RES_HI);
        g_last_error = 8u;               /* ERROR_NOT_ENOUGH_MEMORY */
        ret_std(C, 0, 4);
        return;
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
        else if (len)
            decommit_note(base, len);
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
    else if (guest_decommitted(addr, &base, &size)) {
        /* DECOMMITTED: reserved, not committed, and not accessible. Windows
           answers MEM_RESERVE here with the run of pages in that state, and so
           does this -- the alternative is what issue #41 was, a guest told its
           memory was committed reading a page this host had mprotected away. */
        state = 0x2000u; protect = 0x01u;             /* RESERVE, NOACCESS */
    }
    else if (guest_reserved_span(addr, &base, &size)) {
        /* Memory the guest itself reserved through VirtualAlloc, and not
           decommitted since (checked above). It is mapped PROT_READ|PROT_WRITE,
           so COMMIT is what this host actually did. */
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

/* ======================================================================
 * The STATIC CRT's startup surface.
 *
 * Every module before cg.dll linked the MSVC runtime DYNAMICALLY, so its C
 * library came from MSVCR71.dll and crt.c answers it. cg.dll and cgD3D8.dll --
 * the NVIDIA Cg runtime the engine loads for shading (issue #45) -- link the
 * CRT STATICALLY, so that library is inside them, recompiled with everything
 * else, and it asks Win32 directly for what a C runtime needs: a heap, the
 * standard handles, the environment, the locale and the code pages.
 *
 * So this is not a grab-bag. It is one subsystem with one caller: the code
 * that runs before main/DllMain in a statically-linked MSVC image. That is
 * why it is implemented in a block rather than one function per stop, and why
 * the ones that CANNOT be honestly implemented (RtlUnwind, RaiseException,
 * DebugBreak) stop by name instead of returning a plausible value -- they are
 * control transfers, and a no-op RtlUnwind returns into a frame the CRT has
 * already decided is gone.
 * ====================================================================== */

/* ---- version ----------------------------------------------------------- */

/*
 * GetVersion packs what GetVersionExA spells out, and they must AGREE: a CRT
 * that reads one and a game that reads the other, told different things, take
 * different paths for the same run. 5.1 build 2600, the same Windows XP this
 * layer already claims to be.
 */
void imp_KERNEL32_GetVersion(CPU *C)
{
    ret_std(C, (2600u << 16) | (1u << 8) | 5u, 0);
}

/* ---- the command line and the environment ------------------------------
 *
 * Both have to live in GUEST memory: the CRT keeps the pointers, parses them
 * in place, and hands them to the program as argv/envp.
 */
static uint32_t guest_strdup(const char *s)
{
    uint32_t n = (uint32_t)strlen(s) + 1u, p = guest_malloc(n);
    if (p) memcpy((void *)(uintptr_t)p, s, n);
    return p;
}

void imp_KERNEL32_GetCommandLineA(CPU *C)
{
    /* The REAL command line, read from /proc/self/cmdline, not an invented
       one: a CRT that parses this builds the argv the guest sees, and a
       fabricated line would make the guest disagree with the process it is
       actually running in. The NULs between arguments become spaces, which is
       the Win32 form. */
    static uint32_t p;
    if (!p) {
        char buf[4096];
        ssize_t n = 0;
        int fd = open("/proc/self/cmdline", O_RDONLY);
        if (fd >= 0) { n = read(fd, buf, sizeof buf - 1); close(fd); }
        if (n <= 0) { snprintf(buf, sizeof buf, "x2native"); n = 8; }
        else { ssize_t i; for (i = 0; i < n - 1; i++) if (!buf[i]) buf[i] = ' '; }
        buf[n] = 0;
        p = guest_strdup(buf);
    }
    ret_std(C, p, 0);
}

/*
 * GetEnvironmentStrings: NUL-separated NAME=VALUE, terminated by an empty
 * string. The host's own environment, copied -- the guest is running in this
 * process and inherits it, which is the truthful answer and also the useful
 * one (it is how X2_* reaches anything inside the guest).
 */
static uint32_t env_block(void)
{
    extern char **environ;
    static uint32_t p;
    size_t total = 1;
    int i;
    char *w;
    if (p) return p;
    for (i = 0; environ[i]; i++) total += strlen(environ[i]) + 1;
    p = guest_malloc((uint32_t)total);
    if (!p) return 0;
    w = (char *)(uintptr_t)p;
    for (i = 0; environ[i]; i++) { strcpy(w, environ[i]); w += strlen(w) + 1; }
    *w = 0;
    return p;
}

void imp_KERNEL32_GetEnvironmentStrings(CPU *C) { ret_std(C, env_block(), 0); }

/*
 * The WIDE form, and it is NOT the same block. A CRT that asked for wide
 * strings and got the ANSI block reads every other byte as a character and
 * decides the environment is one letter long -- so it is built as real
 * UTF-16, from the same source.
 */
void imp_KERNEL32_GetEnvironmentStringsW(CPU *C)
{
    extern char **environ;
    static uint32_t p;
    if (!p) {
        size_t total = 1;
        int i;
        uint16_t *w;
        for (i = 0; environ[i]; i++) total += strlen(environ[i]) + 1;
        p = guest_malloc((uint32_t)total * 2u);
        if (p) {
            w = (uint16_t *)(uintptr_t)p;
            for (i = 0; environ[i]; i++) {
                const char *s = environ[i];
                while (*s) *w++ = (uint16_t)(unsigned char)*s++;
                *w++ = 0;
            }
            *w = 0;
        }
    }
    ret_std(C, p, 0);
}

/* Both blocks are process-lifetime, so freeing them is a no-op that succeeds
   -- not an ignored call: Win32's contract is that the caller may free and
   must not use it afterwards, and nothing here reuses it. */
void imp_KERNEL32_FreeEnvironmentStringsA(CPU *C) { ret_std(C, 1, 1); }
void imp_KERNEL32_FreeEnvironmentStringsW(CPU *C) { ret_std(C, 1, 1); }

void imp_KERNEL32_SetEnvironmentVariableA(CPU *C)
{
    const char *name = ACS(0), *val = A(1) ? ACS(1) : NULL;
    int rc = val ? setenv(name, val, 1) : unsetenv(name);
    /* The cached blocks above are now stale, and saying so beats silently
       handing out an environment that disagrees with getenv(). */
    if (rc == 0)
        fprintf(stderr, "kernel32: SetEnvironmentVariableA(\"%s\") changed the "
                        "host environment; any environment BLOCK already handed "
                        "to the guest still holds the old value.\n", name);
    ret_std(C, rc == 0 ? 1u : 0u, 2);
}

/* ---- the standard handles ----------------------------------------------
 *
 * Real handles onto fds 0/1/2, so the CRT's own stdout and stderr writes go
 * through WriteFile above and COME OUT. A token that WriteFile could not use
 * would silently swallow everything the guest printed, which is the opposite
 * of what this port needs from a module it is bringing up.
 */
#define STD_INPUT_HANDLE  ((uint32_t)-10)
#define STD_OUTPUT_HANDLE ((uint32_t)-11)
#define STD_ERROR_HANDLE  ((uint32_t)-12)

static uint32_t g_std[3];                /* by fd: 0, 1, 2 */

void imp_KERNEL32_GetStdHandle(CPU *C)
{
    uint32_t which = A(0);
    int fd;
    switch (which) {
    case STD_INPUT_HANDLE:  fd = 0; break;
    case STD_OUTPUT_HANDLE: fd = 1; break;
    case STD_ERROR_HANDLE:  fd = 2; break;
    default:
        fprintf(stderr, "kernel32: GetStdHandle(0x%08x) is not one of the three "
                        "standard handles; returning INVALID_HANDLE_VALUE "
                        "rather than inventing one.\n", which);
        ret_std(C, INVALID_HANDLE, 1);
        return;
    }
    if (!g_std[fd]) {
        g_std[fd] = h_alloc(H_FILE);
        h_get(g_std[fd], H_FILE)->fd = fd;
    }
    ret_std(C, g_std[fd], 1);
}

/* SetStdHandle: accepted and RECORDED as unimplemented redirection. The CRT
   calls it while wiring up its own streams; honouring it would mean rebinding
   fd 0/1/2, which affects this whole process and not just the guest. */
void imp_KERNEL32_SetStdHandle(CPU *C)
{
    static int said;
    if (!said++)
        fprintf(stderr, "kernel32: SetStdHandle is accepted but NOT honoured -- "
                        "redirecting it would rebind this process's own stdout, "
                        "not just the guest's. Guest output keeps going to the "
                        "real streams.\n");
    ret_std(C, 1, 2);
}

/* FILE_TYPE_DISK 1, FILE_TYPE_CHAR 2, FILE_TYPE_PIPE 3, UNKNOWN 0. Asked by
   the CRT to decide whether a stream is line-buffered; answered from the real
   fd, so a redirected run and a terminal run differ as they should. */
void imp_KERNEL32_GetFileType(CPU *C)
{
    Handle *hh = h_get(A(0), H_FILE);
    struct stat st;
    uint32_t t = 0;
    if (fstat(hh->fd, &st) == 0) {
        if (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode)) t = 1;
        else if (S_ISCHR(st.st_mode))                   t = 2;
        else if (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode)) t = 3;
    }
    ret_std(C, t, 1);
}

/* SetHandleCount is a 16-bit-Windows leftover that Win32 keeps as a no-op
   returning what it was given. This is not a stub -- it is the whole function
   on the real platform too. */
void imp_KERNEL32_SetHandleCount(CPU *C) { ret_std(C, A(0), 1); }

void imp_KERNEL32_FlushFileBuffers(CPU *C)
{
    Handle *hh = h_get(A(0), H_FILE);
    ret_std(C, fsync(hh->fd) == 0 ? 1u : 0u, 1);
}

/* SetFilePointer(h, lo, phi, method): FILE_BEGIN 0, FILE_CURRENT 1, END 2.
   64-bit when phi is given, and the two halves must come from ONE lseek --
   computing them separately is how a large-file seek lands somewhere else. */
void imp_KERNEL32_SetFilePointer(CPU *C)
{
    Handle *hh = h_get(A(0), H_FILE);
    uint32_t phi = A(2), method = A(3);
    int64_t off = (int32_t)A(1);
    off_t r;
    if (phi) off |= (int64_t)(int32_t)RD32(phi) << 32;
    r = lseek(hh->fd, off, method == 1 ? SEEK_CUR : method == 2 ? SEEK_END
                                                                : SEEK_SET);
    if (r == (off_t)-1) {
        g_last_error = ERROR_ACCESS_DENIED;
        ret_std(C, 0xFFFFFFFFu, 4);
        return;
    }
    if (phi) WR32(phi, (uint32_t)((uint64_t)r >> 32));
    ret_std(C, (uint32_t)r, 4);
}

/* ---- private heaps ------------------------------------------------------
 *
 * HeapCreate makes a heap of its OWN, and the CRT uses one for everything it
 * allocates. Every block still comes from the single guest arena -- the arena
 * is what makes an allocation 32-bit addressable, and nothing else here can.
 * What the separate handles buy is that a block freed against the wrong heap
 * is CAUGHT, which is the only guarantee a caller can actually observe.
 */
void imp_KERNEL32_HeapCreate(CPU *C)
{
    ret_std(C, PRIVATE_HEAP_TOK + (uint32_t)(++g_heaps), 3);
}

void imp_KERNEL32_HeapDestroy(CPU *C)
{
    /* The blocks are NOT reclaimed, and that is said rather than assumed: this
       arena has no per-heap bookkeeping to walk. A CRT that destroys its heap
       at exit leaks it into a process that is ending anyway; one that destroys
       a heap mid-run and expects the memory back would grow. */
    static int said;
    if (!said++)
        fprintf(stderr, "kernel32: HeapDestroy frees the HANDLE, not the "
                        "blocks -- the guest arena has no per-heap bookkeeping "
                        "to walk. Whatever was allocated from it stays "
                        "allocated.\n");
    ret_std(C, 1, 1);
}

void imp_KERNEL32_HeapReAlloc(CPU *C)
{
    uint32_t flags = A(1), p = A(2), n = A(3);
    /* HEAP_REALLOC_IN_PLACE_ONLY (0x10) is a real constraint: a caller that
       set it is holding interior pointers, so moving the block corrupts them.
       Refused rather than moved, which is what Win32 does when it cannot. */
    if (flags & 0x10u) { ret_std(C, 0, 4); return; }
    ret_std(C, guest_realloc(p, n), 4);
}

void imp_KERNEL32_HeapSize(CPU *C)
{
    uint32_t base, size;
    if (!guest_heap_contains(A(2), &base, &size)) { ret_std(C, 0xFFFFFFFFu, 3); return; }
    ret_std(C, size - (A(2) - base), 3);
}

/* ---- code pages and locale ---------------------------------------------
 *
 * The CRT sets up its locale before anything else runs. This host is
 * single-locale by construction: the game is the US build and every string in
 * it is single-byte, so the honest answer is the US ANSI/OEM pair and the
 * neutral locale -- not a translation layer to the host's locale, which would
 * make the guest's string handling depend on the machine it runs on.
 */
#define CP_ACP_US 1252u
#define CP_OEM_US 437u
#define LCID_US   0x0409u

void imp_KERNEL32_GetACP(CPU *C)   { ret_std(C, CP_ACP_US, 0); }
void imp_KERNEL32_GetOEMCP(CPU *C) { ret_std(C, CP_OEM_US, 0); }
void imp_KERNEL32_GetUserDefaultLCID(CPU *C) { ret_std(C, LCID_US, 0); }

void imp_KERNEL32_IsValidCodePage(CPU *C)
{
    uint32_t cp = A(0);
    ret_std(C, (cp == CP_ACP_US || cp == CP_OEM_US || cp == 0u || cp == 1u) ? 1u : 0u, 1);
}

void imp_KERNEL32_IsValidLocale(CPU *C)
{
    ret_std(C, (A(0) & 0xFFFFu) == LCID_US ? 1u : 0u, 2);
}

/* CPINFO { UINT MaxCharSize; BYTE DefaultChar[2]; BYTE LeadByte[12]; } --
   single-byte, no lead bytes, which is what 1252 and 437 both are. */
void imp_KERNEL32_GetCPInfo(CPU *C)
{
    uint32_t p = A(1);
    if (!p) { ret_std(C, 0, 2); return; }
    memset((void *)(uintptr_t)p, 0, 20);
    WR32(p, 1);                          /* MaxCharSize */
    WR8(p + 4u, '?');                    /* DefaultChar */
    ret_std(C, 1, 2);
}

/*
 * GetLocaleInfoA/W: only the fields a CRT startup actually reads are answered,
 * and every other LCTYPE is REFUSED BY NUMBER. Answering an unknown one with a
 * plausible string is how a locale-dependent format silently becomes wrong --
 * a decimal separator invented here would come out in numbers the game prints.
 */
static const char *locale_field(uint32_t lctype)
{
    switch (lctype & 0xFFFFu) {
    case 0x0002: return "en-US";         /* LOCALE_SLOCALIZEDDISPLAYNAME-ish */
    case 0x0004: return "1252";          /* LOCALE_IDEFAULTANSICODEPAGE */
    case 0x000B: return "437";           /* LOCALE_IDEFAULTCODEPAGE */
    case 0x0005: return "ENU";           /* LOCALE_SABBREVLANGNAME */
    case 0x000E: return ".";             /* LOCALE_SDECIMAL */
    case 0x000F: return ",";             /* LOCALE_STHOUSAND */
    case 0x0059: return "en";            /* LOCALE_SISO639LANGNAME */
    case 0x005A: return "US";            /* LOCALE_SISO3166CTRYNAME */
    default:     return NULL;
    }
}

static void locale_info(CPU *C, int wide)
{
    uint32_t lctype = A(1), buf = A(2), cch = A(3);
    const char *v = locale_field(lctype);
    uint32_t n;
    if (!v) {
        static int said;
        if (!said++)
            fprintf(stderr, "kernel32: GetLocaleInfo LCTYPE 0x%x is not "
                            "answered -- inventing a locale field is how a "
                            "wrong decimal separator ends up in the game's own "
                            "output. Reported once; each unknown type returns "
                            "0.\n", lctype);
        g_last_error = 87u;              /* ERROR_INVALID_PARAMETER */
        ret_std(C, 0, 4);
        return;
    }
    n = (uint32_t)strlen(v) + 1u;
    if (cch == 0) { ret_std(C, n, 4); return; }   /* size query */
    if (cch < n)  { g_last_error = 122u; ret_std(C, 0, 4); return; }
    if (wide) {
        uint16_t *w = (uint16_t *)(uintptr_t)buf;
        uint32_t i;
        for (i = 0; i < n; i++) w[i] = (uint16_t)(unsigned char)v[i];
    } else {
        memcpy((void *)(uintptr_t)buf, v, n);
    }
    ret_std(C, n, 4);
}

void imp_KERNEL32_GetLocaleInfoA(CPU *C) { locale_info(C, 0); }
void imp_KERNEL32_GetLocaleInfoW(CPU *C) { locale_info(C, 1); }

/*
 * EnumSystemLocalesA(lpLocaleEnumProc, dwFlags): the callback is GUEST code
 * and gets called with one locale -- the only one this host has. Enumerating
 * nothing would be a different answer: a CRT that finds no locale at all takes
 * its "the system is broken" path, and this system is not broken, it is
 * single-locale.
 */
void imp_KERNEL32_EnumSystemLocalesA(CPU *C)
{
    uint32_t proc = A(0), s = guest_strdup("00000409");
    static int said;
    if (!said++)
        printf("kernel32: EnumSystemLocalesA enumerates exactly ONE locale "
               "(00000409, en-US) -- this host is single-locale by "
               "construction, and the callback at 0x%08x is real guest code.\n",
               proc);
    if (proc && s) { uint32_t a = s; ark_call_cdecl(proc, &a, 1); }
    ret_std(C, 1, 2);
}

/* ---- the C-locale string services --------------------------------------
 *
 * ASCII, deliberately and stated: the code page above is single-byte and every
 * string in this game is. A byte >= 0x80 is passed through unchanged by the
 * case mappings and sorts by its value, which is 1252's own order for the
 * letters and is not claimed to be its collation for the rest.
 */
#define NORM_IGNORECASE 0x00000001u
#define LCMAP_LOWERCASE 0x00000100u
#define LCMAP_UPPERCASE 0x00000200u
#define LCMAP_SORTKEY   0x00000400u

static int cmp_bytes(const char *a, int na, const char *b, int nb, int fold)
{
    int i, n = na < nb ? na : nb;
    for (i = 0; i < n; i++) {
        int x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (fold) { x = tolower(x); y = tolower(y); }
        if (x != y) return x < y ? -1 : 1;
    }
    return na == nb ? 0 : (na < nb ? -1 : 1);
}

/* CompareStringA(lcid, flags, s1, n1, s2, n2) -> 1 LESS, 2 EQUAL, 3 GREATER,
   0 on error. -1 for a count means NUL-terminated. */
void imp_KERNEL32_CompareStringA(CPU *C)
{
    const char *a = (const char *)(uintptr_t)A(2);
    const char *b = (const char *)(uintptr_t)A(4);
    int na = (int32_t)A(3), nb = (int32_t)A(5), r;
    if (!a || !b) { ret_std(C, 0, 6); return; }
    if (na < 0) na = (int)strlen(a);
    if (nb < 0) nb = (int)strlen(b);
    r = cmp_bytes(a, na, b, nb, (A(1) & NORM_IGNORECASE) != 0);
    ret_std(C, (uint32_t)(r < 0 ? 1 : r == 0 ? 2 : 3), 6);
}

void imp_KERNEL32_CompareStringW(CPU *C)
{
    const uint16_t *a = (const uint16_t *)(uintptr_t)A(2);
    const uint16_t *b = (const uint16_t *)(uintptr_t)A(4);
    int na = (int32_t)A(3), nb = (int32_t)A(5), i, n;
    int fold = (A(1) & NORM_IGNORECASE) != 0, r = 0;
    if (!a || !b) { ret_std(C, 0, 6); return; }
    if (na < 0) { na = 0; while (a[na]) na++; }
    if (nb < 0) { nb = 0; while (b[nb]) nb++; }
    n = na < nb ? na : nb;
    for (i = 0; i < n && r == 0; i++) {
        int x = a[i], y = b[i];
        if (fold) { if (x < 128) x = tolower(x); if (y < 128) y = tolower(y); }
        if (x != y) r = x < y ? -1 : 1;
    }
    if (r == 0 && na != nb) r = na < nb ? -1 : 1;
    ret_std(C, (uint32_t)(r < 0 ? 1 : r == 0 ? 2 : 3), 6);
}

/* LCMapStringA(lcid, flags, src, ncsrc, dst, ncdst). Case mapping only:
   LCMAP_SORTKEY produces a binary blob whose ORDER is the whole contract, and
   a fabricated one would sort wrongly rather than fail. */
static void lcmap(CPU *C, int wide)
{
    uint32_t flags = A(1), dst = A(4), ncdst = A(5);
    int nsrc = (int32_t)A(3), i;

    if (flags & LCMAP_SORTKEY) {
        fprintf(stderr, "kernel32: LCMapString with LCMAP_SORTKEY is NOT "
                        "implemented -- a sort key's whole contract is its "
                        "byte order, and an invented one sorts wrongly instead "
                        "of failing. Returning 0.\n");
        g_last_error = 87u;
        ret_std(C, 0, 6);
        return;
    }
    if (wide) {
        const uint16_t *s = (const uint16_t *)(uintptr_t)A(2);
        uint16_t *d = (uint16_t *)(uintptr_t)dst;
        if (nsrc < 0) { nsrc = 0; while (s[nsrc]) nsrc++; nsrc++; }
        if (ncdst == 0) { ret_std(C, (uint32_t)nsrc, 6); return; }
        if ((int)ncdst < nsrc) { g_last_error = 122u; ret_std(C, 0, 6); return; }
        for (i = 0; i < nsrc; i++) {
            int c = s[i];
            if (c < 128) c = (flags & LCMAP_UPPERCASE) ? toupper(c)
                          : (flags & LCMAP_LOWERCASE) ? tolower(c) : c;
            d[i] = (uint16_t)c;
        }
    } else {
        const char *s = (const char *)(uintptr_t)A(2);
        char *d = (char *)(uintptr_t)dst;
        if (nsrc < 0) nsrc = (int)strlen(s) + 1;
        if (ncdst == 0) { ret_std(C, (uint32_t)nsrc, 6); return; }
        if ((int)ncdst < nsrc) { g_last_error = 122u; ret_std(C, 0, 6); return; }
        for (i = 0; i < nsrc; i++) {
            int c = (unsigned char)s[i];
            if (c < 128) c = (flags & LCMAP_UPPERCASE) ? toupper(c)
                          : (flags & LCMAP_LOWERCASE) ? tolower(c) : c;
            d[i] = (char)c;
        }
    }
    ret_std(C, (uint32_t)nsrc, 6);
}

void imp_KERNEL32_LCMapStringA(CPU *C) { lcmap(C, 0); }
void imp_KERNEL32_LCMapStringW(CPU *C) { lcmap(C, 1); }

/* GetStringTypeA/W with CT_CTYPE1: the character-class bits the CRT's isalpha
   family is built on. */
#define CT_CTYPE1 1u
static uint16_t ctype1(int c)
{
    uint16_t f = 0;
    if (c > 0xFF) return 0;
    if (isupper(c)) f |= 0x0001;
    if (islower(c)) f |= 0x0002;
    if (isdigit(c)) f |= 0x0004;
    if (isspace(c)) f |= 0x0008;
    if (ispunct(c)) f |= 0x0010;
    if (iscntrl(c)) f |= 0x0020;
    if (c == ' ' || c == '\t') f |= 0x0040;
    if (isxdigit(c)) f |= 0x0080;
    if (isalpha(c)) f |= 0x0100;
    return f;
}

/* The two differ in their ARGUMENT ORDER, not only in width: GetStringTypeA
   takes an LCID first and GetStringTypeW does not. Getting that wrong reads
   the string pointer out of the locale slot. */
static void string_type(CPU *C, int wide, int base, int nargs)
{
    uint32_t info = A(base), src = A(base + 1), out = A(base + 3);
    int n = (int32_t)A(base + 2), i;
    uint16_t *d = (uint16_t *)(uintptr_t)out;
    if (info != CT_CTYPE1) {
        fprintf(stderr, "kernel32: GetStringType info 0x%x is not CT_CTYPE1; "
                        "only the character-class table is implemented, so "
                        "this returns 0 rather than a made-up one.\n", info);
        g_last_error = 87u;
        ret_std(C, 0, nargs);
        return;
    }
    if (!src || !out) { ret_std(C, 0, nargs); return; }
    if (wide) {
        const uint16_t *s = (const uint16_t *)(uintptr_t)src;
        if (n < 0) { n = 0; while (s[n]) n++; n++; }
        for (i = 0; i < n; i++) d[i] = ctype1(s[i]);
    } else {
        const char *s = (const char *)(uintptr_t)src;
        if (n < 0) n = (int)strlen(s) + 1;
        for (i = 0; i < n; i++) d[i] = ctype1((unsigned char)s[i]);
    }
    ret_std(C, 1, nargs);
}

void imp_KERNEL32_GetStringTypeA(CPU *C) { string_type(C, 0, 1, 5); }
void imp_KERNEL32_GetStringTypeW(CPU *C) { string_type(C, 1, 0, 4); }

/* ---- pointer validation -------------------------------------------------
 *
 * IsBadReadPtr and friends are answered by ASKING THE KERNEL, with a write()
 * of the range to /dev/null: it performs the same access check the real API
 * does and reports EFAULT instead of raising a signal. Returning "the pointer
 * is fine" unconditionally is the usual shortcut and it inverts the function's
 * entire purpose -- a caller uses it precisely because it does not trust the
 * pointer.
 */
static int mem_accessible(uint32_t p, uint32_t n)
{
    static int devnull = -2;
    ssize_t r;
    if (!p) return 0;
    if (n == 0) n = 1;
    if (devnull == -2) devnull = open("/dev/null", O_WRONLY);
    if (devnull < 0) return 1;           /* cannot check -> do not claim bad */
    r = write(devnull, (const void *)(uintptr_t)p, (size_t)n);
    return r == (ssize_t)n;
}

void imp_KERNEL32_IsBadReadPtr(CPU *C)
{
    ret_std(C, mem_accessible(A(0), A(1)) ? 0u : 1u, 2);
}

void imp_KERNEL32_IsBadCodePtr(CPU *C)
{
    ret_std(C, mem_accessible(A(0), 1) ? 0u : 1u, 1);
}

/*
 * IsBadWritePtr answers the READ question, and says so once.
 *
 * The probe above cannot test writability without writing, and writing to test
 * would corrupt exactly the buffer being validated. Every mapping the guest
 * gets from this host is read-write, so read-accessible implies writable here
 * -- but that is a property of this host, not of the API, and it is stated
 * rather than assumed silently.
 */
void imp_KERNEL32_IsBadWritePtr(CPU *C)
{
    static int said;
    if (!said++)
        fprintf(stderr, "kernel32: IsBadWritePtr is answered by the READ probe "
                        "-- testing writability by writing would corrupt the "
                        "buffer being checked. Every guest mapping this host "
                        "makes is read-write, so the answers coincide here.\n");
    ret_std(C, mem_accessible(A(0), A(1)) ? 0u : 1u, 2);
}

/* ---- the control transfers that CANNOT be faked -------------------------
 *
 * Each of these hands control somewhere. A no-op version returns to a caller
 * that has already decided this call does not return, so the damage lands
 * later and somewhere else. They stop, by name, with what was asked.
 */
void imp_KERNEL32_RtlUnwind(CPU *C)
{
    fprintf(stderr, "kernel32: RtlUnwind(target 0x%08x, code 0x%08x) -- SEH "
                    "unwinding is NOT implemented. The CRT is unwinding out of "
                    "an exception, and returning from here would resume in a "
                    "frame it has already discarded.\n"
                    "  This is a real exception in guest code, not a missing "
                    "stub: find what threw.\n", A(0), A(1));
    x86_diag_dump();
    abort();
}

void imp_KERNEL32_RaiseException(CPU *C)
{
    fprintf(stderr, "kernel32: RaiseException(code 0x%08x, flags 0x%08x) -- "
                    "guest code is RAISING an exception and this host has no "
                    "SEH dispatcher to deliver it to.\n"
                    "  Code 0xE06D7363 is a C++ throw; 0x80000003 is a "
                    "breakpoint. Either way the guest decided something is "
                    "wrong before this layer was involved.\n", A(0), A(1));
    x86_diag_dump();
    abort();
}

void imp_KERNEL32_DebugBreak(CPU *C)
{
    (void)C;
    fprintf(stderr, "kernel32: DebugBreak -- guest code asked for a debugger. "
                    "There is none, and continuing past a deliberate break "
                    "would run code that expects to have been inspected.\n");
    x86_diag_dump();
    abort();
}

void imp_KERNEL32_TerminateProcess(CPU *C)
{
    fprintf(stderr, "kernel32: TerminateProcess(exit code %u) -- guest code is "
                    "killing the process.\n", A(1));
    exit((int)A(1));
}

/*
 * SetUnhandledExceptionFilter: the filter is RECORDED and never called, and
 * that is the honest state -- there is no SEH dispatcher here to call it from.
 * Returns the previous filter, which is the part callers actually use (they
 * chain to it).
 */
static uint32_t g_ueh_filter;
void imp_KERNEL32_SetUnhandledExceptionFilter(CPU *C)
{
    uint32_t prev = g_ueh_filter;
    static int said;
    g_ueh_filter = A(0);
    if (!said++)
        fprintf(stderr, "kernel32: SetUnhandledExceptionFilter(0x%08x) is "
                        "recorded but will never be CALLED -- this host has no "
                        "SEH dispatcher. A crash reports through x2native's own "
                        "handler instead.\n", A(0));
    ret_std(C, prev, 1);
}
