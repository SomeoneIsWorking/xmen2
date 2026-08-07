/*
 * SHELL32 -- the one call the game makes, and the writable directory behind it.
 *
 * XMen2.exe asks Windows where the user's documents live and puts its saves
 * there. With nothing answering, the first thing the game draws is its own
 * "SAVE FAILED!" dialog (issue #39) -- correctly, because it truthfully has
 * nowhere to write.
 *
 * WHERE THE SAVES GO, and why not the obvious place. This port treats the
 * install as strictly read-only and keeps run state in the repo, so pointing
 * the game at $GAME_PC_DIR would break the one rule the whole project is built
 * on, and pointing it at $HOME/Documents would scatter state across a machine
 * on a run nobody asked to be permanent. The default is `scratch/saves` under
 * the working directory -- which run.sh makes the repo root -- and X2_SAVE_DIR
 * overrides it for anyone who wants their saves elsewhere.
 *
 * The directory is CREATED here, not merely named. Handing back a path that
 * does not exist moves the same failure one step later, into a CreateFileA
 * that fails for a reason nothing connects to this function.
 *
 * THE DRIVE LETTER. The guest gets a Windows path, and win_path() resolves
 * guest paths against the install -- so a POSIX path handed to the guest would
 * come back as $GAME_PC_DIR + the whole thing. It gets a path on a VIRTUAL
 * DRIVE instead, and win_path knows that one letter maps to the save directory
 * rather than to the install. See x2_save_dir() and win_path().
 */
#include "x86rt.h"
#include "x86rt_native.h"
#include "shell32.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define A(i)  RD32(C->esp + 4u + (uint32_t)(i) * 4u)

static void ret_std(CPU *C, uint32_t eax, int nargs)
{
    C->eax = eax;
    C->esp += 4u + (uint32_t)nargs * 4u;
}

/* CSIDL values, and the flag bits that ride on top of them. */
#define CSIDL_PERSONAL        0x0005u   /* My Documents */
#define CSIDL_APPDATA         0x001au
#define CSIDL_LOCAL_APPDATA   0x001cu
#define CSIDL_COMMON_APPDATA  0x0023u
#define CSIDL_MASK            0x00FFu

#define S_OK_HR      0x00000000u
#define E_FAIL_HR    0x80004005u

static char g_dir[1024];
static int  g_ready;

const char *x2_save_dir(void)
{
    const char *env;
    if (g_ready) return g_dir;
    g_ready = 1;
    env = getenv("X2_SAVE_DIR");
    snprintf(g_dir, sizeof g_dir, "%s", (env && *env) ? env : "scratch/saves");
    if (mkdir(g_dir, 0777) != 0 && errno != EEXIST) {
        /* Said once, loudly: everything downstream will fail to open files in
           a directory that is not there, and none of those failures would
           mention this one. */
        fprintf(stderr, "shell32: could not create the save directory \"%s\" "
                        "(%s). The game will report that it cannot save, and "
                        "it will be right.\n", g_dir, strerror(errno));
        g_dir[0] = 0;
    }
    return g_dir;
}

/*
 * SHGetFolderPathA(hwndOwner, nFolder, hToken, dwFlags, pszPath)
 *
 * pszPath is a MAX_PATH buffer the caller owns. The return is an HRESULT.
 */
static void imp_SHELL32_SHGetFolderPathA(CPU *C)
{
    uint32_t folder = A(1) & CSIDL_MASK, out = A(4);
    const char *dir = x2_save_dir();

    if (!out) { ret_std(C, E_FAIL_HR, 5); return; }
    switch (folder) {
    case CSIDL_PERSONAL:
    case CSIDL_APPDATA:
    case CSIDL_LOCAL_APPDATA:
    case CSIDL_COMMON_APPDATA:
        break;
    default:
        /*
         * Refused by number rather than answered with the save directory.
         * Every CSIDL means something different -- a program that asked for
         * the Windows directory and got a save folder would write into it, and
         * the damage would look like anything but this.
         */
        fprintf(stderr, "shell32: SHGetFolderPathA(CSIDL 0x%x) -- this host "
                        "only answers the per-user data folders (5 PERSONAL, "
                        "0x1a APPDATA, 0x1c LOCAL_APPDATA, 0x23 "
                        "COMMON_APPDATA).\n"
                        "  Anything else means something specific and would be "
                        "written to, so it gets E_FAIL rather than a "
                        "plausible directory.\n", folder);
        WR8(out, 0);
        ret_std(C, E_FAIL_HR, 5);
        return;
    }
    if (!dir || !*dir) { WR8(out, 0); ret_std(C, E_FAIL_HR, 5); return; }
    /* The virtual drive, not the host path -- see the file header. NO
       trailing separator: Windows returns "C:\\Users\\x\\Documents", and the
       caller appends its own, so a trailing one here produced
       "S:\\\\Activision\\..." in the very first path the game built. */
    snprintf((char *)(uintptr_t)out, 260, "%c:", X2_SAVE_DRIVE);
    ret_std(C, S_OK_HR, 5);
}

void shell32_install(void)
{
    x86_native_export("SHELL32.DLL", "SHGetFolderPathA",
                      imp_SHELL32_SHGetFolderPathA);
}

void shell32_report(void)
{
    if (!g_ready) {
        printf("  shell32: SHGetFolderPathA was never called, so no save "
               "directory was ever asked for.\n");
        return;
    }
    if (!g_dir[0]) {
        printf("  shell32: the save directory could NOT be created; the game "
               "was told it has nowhere to save.\n");
        return;
    }
    printf("  shell32: saves go to \"%s\", which the guest sees as %c:\\\n",
           g_dir, X2_SAVE_DRIVE);
}
