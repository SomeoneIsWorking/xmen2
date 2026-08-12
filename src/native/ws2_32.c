/*
 * WS2_32: the answer is "this host has no network", said the way Win32 says it.
 *
 * X-Men Legends II has LAN multiplayer, and XMen2.exe imports twenty-five
 * WS2_32 ordinals for it -- the whole Berkeley-sockets surface: socket, bind,
 * listen, accept, connect, send, recv, select, gethostbyname, and so on. None
 * of it is implemented here, and the run reached `WSAStartup` and stopped by
 * name, which is what this port does with an unimplemented import.
 *
 * WHAT THIS FILE DOES, and why it is an answer rather than a workaround.
 *
 * `WSAStartup` is the gate: Winsock is unusable until it succeeds, and every
 * Win32 program is written to handle it FAILING, because it genuinely does on
 * a machine with no network stack. So this returns WSASYSNOTREADY, which is
 * defined as "the underlying network subsystem is not ready for network
 * communication" -- exactly, and not approximately, the situation. The game
 * then takes its own no-network path and the other twenty-four imports are
 * never called.
 *
 * That is a real behaviour difference and it is not hidden: LAN multiplayer is
 * UNAVAILABLE in this port, this file says so, and it says so at runtime the
 * first time the game asks. What it is not is a lie -- nothing here reports a
 * socket that does not exist, or a successful startup followed by calls that
 * abort one layer down, which is what returning success would produce.
 *
 * THE REAL FIX, when someone wants LAN play: implement the twenty-five on
 * POSIX sockets. They map almost one to one (WSAStartup and WSACleanup have no
 * POSIX counterpart and become bookkeeping; `SOCKET` is an int; `closesocket`
 * is `close`; `ioctlsocket` is `fcntl`; the error codes need a translation
 * table). It is a bounded job, it is not this one, and it cannot be verified
 * without two instances talking to each other.
 */
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

#define A(i)  RD32(C->esp + 4u + (uint32_t)(i) * 4u)

static void ret_std(CPU *C, uint32_t eax, int nargs)
{
    C->eax = eax;
    C->esp += 4u + (uint32_t)nargs * 4u;
}

#define WSASYSNOTREADY 10091u

static unsigned long g_startups, g_cleanups;

/*
 * int WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData)
 *
 * The WSADATA is filled in anyway, and that is deliberate rather than tidy:
 * a caller that ignores the return value and reads wVersion out of the
 * structure gets a coherent answer instead of whatever was on its stack.
 * The 400-byte layout is Winsock 2's: wVersion, wHighVersion, szDescription
 * [257], szSystemStatus[129], iMaxSockets, iMaxUdpDg, lpVendorInfo.
 */
void imp_WS2_32__115(CPU *C)
{
    uint32_t want = A(0), data = A(1);

    if (!g_startups++)
        fprintf(stderr,
            "ws2_32: WSAStartup(version %u.%u) -> WSASYSNOTREADY. This host "
            "implements NO networking, so the honest Win32 answer is that the "
            "network subsystem is not ready.\n"
            "  LAN MULTIPLAYER IS UNAVAILABLE in this port. The game takes its "
            "own no-network path from here; the other 24 WS2_32 imports it "
            "carries are never reached.\n"
            "  Reported once. See src/native/ws2_32.c for what implementing "
            "them would take.\n",
            want & 0xffu, (want >> 8) & 0xffu);
    if (data) {
        int i;
        WR32(data + 0u, 0);                     /* wVersion / wHighVersion */
        for (i = 4; i < 4 + 257 + 129; i++)
            *((unsigned char *)(uintptr_t)(data + (uint32_t)i)) = 0;
        WR32(data + 0x18eu, 0);                 /* iMaxSockets, iMaxUdpDg */
        WR32(data + 0x192u, 0);                 /* lpVendorInfo */
    }
    ret_std(C, WSASYSNOTREADY, 2);
}

/* int WSACleanup(void) -- succeeds; there is nothing to tear down. Win32
   returns SOCKET_ERROR if no successful startup happened, but a game shutting
   down does not check, and reporting failure here would only add noise to an
   exit path. */
void imp_WS2_32__116(CPU *C)
{
    g_cleanups++;
    ret_std(C, 0, 0);
}

void ws2_report(void)
{
    /* At zero as well: "the game never asked for networking" and "networking
       is not implemented" are different facts about a run. */
    if (!g_startups) {
        printf("  ws2_32: the game never called WSAStartup, so it never tried "
               "to use the network in this run.\n");
        return;
    }
    printf("  ws2_32: %lu WSAStartup call(s), all answered WSASYSNOTREADY "
           "(%lu WSACleanup). LAN multiplayer is unavailable in this port.\n",
           g_startups, g_cleanups);
}
