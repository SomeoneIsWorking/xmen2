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
    abort();
}

/* ---- handles ----------------------------------------------------------- */

#define MAX_HANDLES 256
#define H_FILE 1
#define H_FIND 2
#define H_MAP  3

typedef struct {
    int   kind;
    int   fd;
    DIR  *dir;
    char  pattern[256];
    char  dirpath[1024];
    void *map;
    size_t maplen;
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

static const char *win_path(const char *in)
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

void imp_KERNEL32_CloseHandle(CPU *C)
{
    Handle *hh = h_get(A(0), 0);
    if (hh->kind == H_FILE && hh->fd >= 0) close(hh->fd);
    if (hh->kind == H_FIND && hh->dir) closedir(hh->dir);
    if (hh->kind == H_MAP && hh->map) munmap(hh->map, hh->maplen);
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

void imp_KERNEL32_LoadLibraryA(CPU *C)
{
    k32_unimpl("LoadLibraryA", "a runtime-loaded module would have to be "
               "recompiled and registered first; returning a fake handle would "
               "make GetProcAddress return fake functions");
}

void imp_KERNEL32_GetProcAddress(CPU *C)
{
    k32_unimpl("GetProcAddress", "see LoadLibraryA");
}

void imp_KERNEL32_FreeLibrary(CPU *C) { ret_std(C, 1, 1); }

/* ---- not yet implemented ----------------------------------------------- */

void imp_KERNEL32_CreateFileW(CPU *C)
{
    k32_unimpl("CreateFileW", "the wide-character file API is unused by the "
               "paths reached so far; implementing it blind would be untested "
               "code on a path nothing exercises");
}

void imp_KERNEL32_CreateFileMappingA(CPU *C)
{
    k32_unimpl("CreateFileMappingA", "file mapping needs the guest to hold the "
               "view below 4 GB, which needs a guest-address arena of its own");
}

void imp_KERNEL32_MapViewOfFile(CPU *C) { k32_unimpl("MapViewOfFile", "see CreateFileMappingA"); }
void imp_KERNEL32_UnmapViewOfFile(CPU *C) { k32_unimpl("UnmapViewOfFile", "see CreateFileMappingA"); }

void imp_KERNEL32_FindFirstFileA(CPU *C)
{
    k32_unimpl("FindFirstFileA", "directory enumeration needs the WIN32_FIND_DATA "
               "layout filled exactly; it is reached only by the asset scanner, "
               "which nothing has run yet");
}
void imp_KERNEL32_FindNextFileA(CPU *C) { k32_unimpl("FindNextFileA", "see FindFirstFileA"); }
void imp_KERNEL32_FindClose(CPU *C) { k32_unimpl("FindClose", "see FindFirstFileA"); }

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
