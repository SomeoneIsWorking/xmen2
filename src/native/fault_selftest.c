/*
 * The --fault-selftest battery, extracted from x2native.c: it exists to prove
 * the fault reporter fires, which is a different concern from installing the
 * reporter. See fault_report.h for the seam; the runtime handler stays in
 * x2native.c beside the install path it serves.
 */
#include "fault_report.h"

#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

static void genuine_illegal_instruction(void)
{
#if defined(__aarch64__)
    __asm__ __volatile__(".inst 0");
#elif defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__("ud2");
#else
    raise(SIGILL);
#endif
}

static int fault_child(int sig, int genuine, int control)
{
    struct sigaction sa;
    static const int fatal[] = { SIGSEGV, SIGILL, SIGFPE, SIGBUS, SIGTRAP };
    size_t i;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = fault_report;
    sa.sa_flags = SA_SIGINFO;
    for (i = 0; i < sizeof fatal / sizeof fatal[0]; i++)
        sigaction(fatal[i], &sa, NULL);
    if (control) { fflush(NULL); _exit(0); }
    if (genuine) genuine_illegal_instruction();
    raise(sig);
    fflush(NULL);
    _exit(0);                    /* the handler _exit(3)s; reaching here fails */
}

int x2_fault_selftest(void)
{
    static const struct { int sig; int genuine; const char *what; } cases[] = {
        { SIGILL,  1, "a real illegal opcode instruction" },
        { SIGFPE,  0, "raise(SIGFPE)" },
        { SIGBUS,  0, "raise(SIGBUS)" },
        { SIGTRAP, 0, "raise(SIGTRAP)" },
        { SIGSEGV, 0, "raise(SIGSEGV)" },
    };
    size_t i;
    int fails = 0;

    for (i = 0; i <= sizeof cases / sizeof cases[0]; i++) {
        int control = (i == sizeof cases / sizeof cases[0]);
        int sig = control ? 0 : cases[i].sig;
        const char *want = control ? NULL : fault_name(sig);
        char buf[8192];
        int fd[2], status = 0;
        pid_t pid;
        size_t got = 0;
        ssize_t n;

        if (pipe(fd) != 0) {
            printf("x2native --fault-selftest: pipe() failed; NOTHING was "
                   "checked.\n");
            return 1;
        }
        fflush(NULL);
        pid = fork();
        if (pid < 0) {
            printf("x2native --fault-selftest: fork() failed; NOTHING was "
                   "checked.\n");
            return 1;
        }
        if (pid == 0) {
            close(fd[0]);
            dup2(fd[1], 2);
            close(fd[1]);
            return fault_child(sig, control ? 0 : cases[i].genuine, control);
        }
        close(fd[1]);
        /* Drain to EOF even once the buffer is full: a child blocked writing
           into a pipe nobody is reading would make waitpid() below hang, and a
           selftest that hangs is worse than one that fails. */
        for (;;) {
            char sink[4096];
            if (got < sizeof buf - 1)
                n = read(fd[0], buf + got, sizeof buf - 1 - got);
            else
                n = read(fd[0], sink, sizeof sink);
            if (n <= 0) break;
            if (got < sizeof buf - 1) got += (size_t)n;
        }
        buf[got] = 0;
        close(fd[0]);
        waitpid(pid, &status, 0);

        if (control) {
            int quiet = (strstr(buf, "***") == NULL);
            int clean = WIFEXITED(status) && WEXITSTATUS(status) == 0;
            printf("  control: handlers installed, no fault  -- %s "
                   "(%zu byte(s) on stderr, exit %d)\n",
                   quiet && clean ? "silent, as it must be"
                                  : "FAILED: it reported a fault that did not "
                                    "happen",
                   got, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            if (!quiet || !clean) fails++;
            continue;
        }
        {
            int named = strstr(buf, want) != NULL;
            int host_pc = strstr(buf, "[HOST PC]") != NULL;
            int reported = WIFEXITED(status) && WEXITSTATUS(status) == 3;
            printf("  %-8s via %-32s -- %s (exit %d, %zu byte(s) reported)\n",
                   want, cases[i].what,
                   named && host_pc && reported ? "reported by name with host PC"
                                     : "FAILED: no report reached stderr",
                   WIFEXITED(status) ? WEXITSTATUS(status)
                                         : -WTERMSIG(status), got);
            if (!named || !host_pc || !reported) fails++;
        }
    }
    printf("x2native --fault-selftest: %s -- %d failure(s). Before this, only "
           "SIGSEGV was handled and every other fatal signal killed the run "
           "with nothing printed.\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
