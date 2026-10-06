/*
 * WS2_32: the game's sockets, on the host's.
 *
 * XMen2.exe imports twenty-five WS2_32 ordinals, and every one of them is the
 * Berkeley surface GameSpy's SDK is written against: UDP sockets, broadcast,
 * select, and a resolver. None is Windows-only in meaning, so each thunk here
 * reads the guest's arguments, hands the translation to winsock_posix.c, and
 * returns what Winsock would. There is no listen or accept in the import
 * table; the game's sessions are datagrams.
 *
 * A call that can block -- a blocking recv, a select with a timeout, a
 * connect -- releases guest ownership while it waits, exactly as a Win32 wait
 * does, so the game's other threads keep running.
 *
 * The browser build answers WSAStartup with WSASYSNOTREADY: a page cannot open
 * a UDP socket, let alone broadcast one, and that is the Win32 answer for "no
 * network subsystem" rather than a socket layer that fails one call later.
 */
#include "guest_memory.h"
#include "stdcall_import.h"
#include "threads.h"
#include "winsock_posix.h"
#include "x2_log.h"
#include "x86rt_native.h"

#include <arpa/inet.h>
#include <errno.h>
#include <lucent/log_c.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

enum {
  WIN_FIONBIO = (int32_t)0x8004667e,
  WIN_FIONREAD = 0x4004667f,
  WIN_SOCKADDR_IN = 16,
  WIN_GUEST_WSADATA = 400
};

static unsigned long g_startups, g_cleanups, g_sent, g_received, g_refused;

/* Return `value` as success, or SOCKET_ERROR with the WSA code recorded. */
static void ret_result(CPU *C, int ok, uint32_t value, uint32_t error,
                       int nargs) {
  if (!ok) {
    winsock_set_last_error(error);
    value = WINSOCK_SOCKET_ERROR;
  }
  ret_std(C, value, nargs);
}

static int not_socket(CPU *C, uint32_t handle, int nargs) {
  if (winsock_is_socket(handle)) {
    return 0;
  }
  ret_result(C, 0, 0, WSAENOTSOCK, nargs);
  return 1;
}

/* Blocking host calls give the guest lock up; a non-blocking one returns at
   once and keeps it. */
static int wait_begin(uint32_t handle) {
  if (!winsock_blocking(handle)) {
    return 0;
  }
  guest_blocking_begin();
  return 1;
}

static void wait_end(int waited) {
  if (waited) {
    guest_blocking_end();
  }
}

/* int WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData) */
void imp_WS2_32__115(CPU *C) {
  const uint32_t want = A(0), data = A(1);
  uint8_t info[WIN_GUEST_WSADATA];
  memset(info, 0, sizeof info);
#ifdef __EMSCRIPTEN__
  const uint32_t result = WSASYSNOTREADY;
#else
  const uint32_t result = 0;
  info[0] = (uint8_t)want;
  info[1] = (uint8_t)(want >> 8);
  info[2] = 2;
  info[3] = 2;
  strcpy((char *)info + 4, "WinSock 2.0 over POSIX sockets");
  strcpy((char *)info + 4 + 257, "Running");
#endif
  if (!g_startups++) {
    lucent_log_info("net", "WSAStartup(%u.%u) -> %u", want & 0xffu,
                    (want >> 8) & 0xffu, result);
  }
  if (data) {
    guest_memory_write(data, info, sizeof info);
  }
  ret_std(C, result, 2);
}

/* int WSACleanup(void) */
void imp_WS2_32__116(CPU *C) {
  g_cleanups++;
  ret_std(C, 0, 0);
}

/* int WSAGetLastError(void) */
void imp_WS2_32__111(CPU *C) { ret_std(C, winsock_last_error(), 0); }

/* SOCKET socket(int af, int type, int protocol) */
void imp_WS2_32__23(CPU *C) {
  uint32_t error = 0;
  const int fd =
      winsock_socket_open((int32_t)A(0), (int32_t)A(1), (int32_t)A(2), &error);
  if (fd < 0) {
    winsock_set_last_error(error);
    ret_std(C, WINSOCK_INVALID_SOCKET, 3);
    return;
  }
  ret_std(C, (uint32_t)fd, 3);
}

/* int closesocket(SOCKET s) */
void imp_WS2_32__3(CPU *C) {
  uint32_t error = 0;
  ret_result(C, winsock_close(A(0), &error), 0, error, 1);
}

/* bind(s, name, namelen) and connect(s, name, namelen) share their address
   translation; connect is the one that can wait. */
static void address_call(CPU *C, int connecting) {
  const uint32_t s = A(0);
  struct sockaddr_in host;
  uint32_t error = 0;
  if (not_socket(C, s, 3)) {
    return;
  }
  if (!winsock_sockaddr_to_host(guest_memory_span(A(1), WIN_SOCKADDR_IN),
                                (int32_t)A(2), &host, &error)) {
    ret_result(C, 0, 0, error, 3);
    return;
  }
  if (!connecting) {
    const int bound = winsock_bind(s, &host, &error);
    x2_log_info("ws2_32: bind(%u, port %u) -> %s", s, ntohs(host.sin_port),
                bound ? "bound" : "refused");
    ret_result(C, bound, 0, error, 3);
    return;
  }
  const int waited = wait_begin(s);
  const int rc = connect((int)s, (struct sockaddr *)&host, sizeof host);
  const int err = errno;
  wait_end(waited);
  ret_result(C, rc == 0, 0, winsock_error_from_errno(err), 3);
}

void imp_WS2_32__2(CPU *C) { address_call(C, 0); }

void imp_WS2_32__4(CPU *C) { address_call(C, 1); }

/* int getsockname(SOCKET s, struct sockaddr *name, int *namelen) */
void imp_WS2_32__6(CPU *C) {
  const uint32_t s = A(0), name = A(1), length = A(2);
  struct sockaddr_in host;
  uint32_t available = 0, error = 0;
  if (not_socket(C, s, 3)) {
    return;
  }
  if (!guest_memory_try_read32(length, &available) ||
      available < WIN_SOCKADDR_IN ||
      !guest_memory_span(name, WIN_SOCKADDR_IN)) {
    ret_result(C, 0, 0, WSAEFAULT, 3);
    return;
  }
  if (!winsock_getsockname(s, &host, &error)) {
    ret_result(C, 0, 0, error, 3);
    return;
  }
  winsock_sockaddr_from_host(&host, guest_memory_pointer(name));
  WR32(length, WIN_SOCKADDR_IN);
  ret_std(C, 0, 3);
}

/* int shutdown(SOCKET s, int how): SD_RECEIVE/SEND/BOTH are SHUT_RD/WR/RDWR. */
void imp_WS2_32__22(CPU *C) {
  const uint32_t s = A(0);
  if (not_socket(C, s, 2)) {
    return;
  }
  const int rc = shutdown((int)s, (int)A(1));
  ret_result(C, rc == 0, 0, winsock_error_from_errno(errno), 2);
}

/* int ioctlsocket(SOCKET s, long cmd, u_long *argp) */
void imp_WS2_32__10(CPU *C) {
  const uint32_t s = A(0), argp = A(2);
  const int32_t cmd = (int32_t)A(1);
  uint32_t error = 0, value = 0;
  if (not_socket(C, s, 3)) {
    return;
  }
  if (!guest_memory_try_read32(argp, &value)) {
    ret_result(C, 0, 0, WSAEFAULT, 3);
  } else if (cmd == WIN_FIONBIO) {
    ret_result(C, winsock_set_blocking(s, value == 0, &error), 0, error, 3);
  } else if (cmd == WIN_FIONREAD) {
    int pending = 0;
    const int ok = ioctl((int)s, FIONREAD, &pending) == 0;
    if (ok) {
      WR32(argp, (uint32_t)pending);
    }
    ret_result(C, ok, 0, winsock_error_from_errno(errno), 3);
  } else {
    g_refused++;
    x2_log_error("ws2_32: ioctlsocket command 0x%08x is not translated\n",
                 (unsigned)cmd);
    ret_result(C, 0, 0, WSAEINVAL, 3);
  }
}

/* int setsockopt(SOCKET s, int level, int optname, const char *optval,
                  int optlen) -- integer options; a shorter optlen (a BOOL
                  passed as one byte) is widened. */
void imp_WS2_32__21(CPU *C) {
  const uint32_t s = A(0), optval = A(3), optlen = A(4);
  const int32_t level = (int32_t)A(1), name = A(2);
  int host_level = 0, host_name = 0, value = 0;
  if (not_socket(C, s, 5)) {
    return;
  }
  if (!winsock_sockopt_to_host(level, name, &host_level, &host_name)) {
    g_refused++;
    x2_log_error("ws2_32: setsockopt level 0x%x option 0x%x is not "
                 "translated\n",
                 (unsigned)level, (unsigned)name);
    ret_result(C, 0, 0, WSAENOPROTOOPT, 5);
    return;
  }
  if (!optlen || optlen > 4 || !guest_memory_try_read(optval, &value, optlen)) {
    ret_result(C, 0, 0, WSAEFAULT, 5);
    return;
  }
  const int rc =
      setsockopt((int)s, host_level, host_name, &value, sizeof value);
  ret_result(C, rc == 0, 0, winsock_error_from_errno(errno), 5);
}

/* send(s, buf, len, flags) and recv(s, buf, len, flags). */
static void stream_call(CPU *C, int sending) {
  const uint32_t s = A(0), buffer = A(1), length = A(2);
  if (not_socket(C, s, 4)) {
    return;
  }
  void *host = length ? guest_memory_span(buffer, length) : NULL;
  if (length && !host) {
    ret_result(C, 0, 0, WSAEFAULT, 4);
    return;
  }
  const int flags = (int)(A(3) & 7u);
  const int waited = wait_begin(s);
  const ssize_t n = sending ? send((int)s, host, length, flags | SEND_FLAGS)
                            : recv((int)s, host, length, flags);
  const int err = errno;
  wait_end(waited);
  if (n > 0) {
    *(sending ? &g_sent : &g_received) += 1;
  }
  ret_result(C, n >= 0, (uint32_t)n, winsock_error_from_errno(err), 4);
}

void imp_WS2_32__19(CPU *C) { stream_call(C, 1); }

void imp_WS2_32__16(CPU *C) { stream_call(C, 0); }

/* int sendto(SOCKET s, const char *buf, int len, int flags,
              const struct sockaddr *to, int tolen) */
void imp_WS2_32__20(CPU *C) {
  const uint32_t s = A(0), buffer = A(1), length = A(2);
  struct sockaddr_in host;
  uint32_t error = 0;
  if (not_socket(C, s, 6)) {
    return;
  }
  const void *data = length ? guest_memory_span(buffer, length) : NULL;
  if ((length && !data) ||
      !winsock_sockaddr_to_host(guest_memory_span(A(4), WIN_SOCKADDR_IN),
                                (int32_t)A(5), &host, &error)) {
    ret_result(C, 0, 0, error ? error : WSAEFAULT, 6);
    return;
  }
  const int waited = wait_begin(s);
  const ssize_t n = sendto((int)s, data, length, (int)(A(3) & 7u) | SEND_FLAGS,
                           (struct sockaddr *)&host, sizeof host);
  const int err = errno;
  wait_end(waited);
  g_sent += n >= 0;
  ret_result(C, n >= 0, (uint32_t)n, winsock_error_from_errno(err), 6);
}

/* int recvfrom(SOCKET s, char *buf, int len, int flags,
                struct sockaddr *from, int *fromlen) */
void imp_WS2_32__17(CPU *C) {
  const uint32_t s = A(0), buffer = A(1), length = A(2), from = A(4),
                 fromlen = A(5);
  struct sockaddr_in host;
  socklen_t size = sizeof host;
  uint32_t available = 0;
  if (not_socket(C, s, 6)) {
    return;
  }
  void *data = length ? guest_memory_span(buffer, length) : NULL;
  if ((length && !data) ||
      (from && (!guest_memory_try_read32(fromlen, &available) ||
                available < WIN_SOCKADDR_IN ||
                !guest_memory_span(from, WIN_SOCKADDR_IN)))) {
    ret_result(C, 0, 0, WSAEFAULT, 6);
    return;
  }
  const int waited = wait_begin(s);
  const ssize_t n = recvfrom((int)s, data, length, (int)(A(3) & 7u),
                             (struct sockaddr *)&host, &size);
  const int err = errno;
  wait_end(waited);
  if (n >= 0 && from) {
    winsock_sockaddr_from_host(&host, guest_memory_pointer(from));
    WR32(fromlen, WIN_SOCKADDR_IN);
  }
  g_received += n >= 0;
  ret_result(C, n >= 0, (uint32_t)n, winsock_error_from_errno(err), 6);
}

/* int select(int nfds, fd_set *read, fd_set *write, fd_set *except,
              const struct timeval *timeout) -- nfds is ignored, as on
              Windows. */
void imp_WS2_32__18(CPU *C) {
  WinsockFdSet *sets[3] = {NULL, NULL, NULL};
  int32_t timeval[2] = {0, 0};
  uint32_t error = 0;
  for (int i = 0; i < 3; ++i) {
    const uint32_t set = A(1 + i);
    sets[i] = set ? guest_memory_span(set, sizeof(WinsockFdSet)) : NULL;
    if (set && !sets[i]) {
      ret_result(C, 0, 0, WSAEFAULT, 5);
      return;
    }
  }
  if (A(4) && !guest_memory_try_read(A(4), timeval, sizeof timeval)) {
    ret_result(C, 0, 0, WSAEFAULT, 5);
    return;
  }
  const int64_t timeout_us =
      A(4) ? (int64_t)timeval[0] * 1000000 + timeval[1] : -1;
  const int waited = timeout_us != 0;
  if (waited) {
    guest_blocking_begin();
  }
  const int ready =
      winsock_select(sets[0], sets[1], sets[2], timeout_us, &error);
  wait_end(waited);
  ret_result(C, ready >= 0, (uint32_t)ready, error, 5);
}

/* int __WSAFDIsSet(SOCKET s, fd_set *set) */
void imp_WS2_32__151(CPU *C) {
  const WinsockFdSet *set = guest_memory_span(A(1), sizeof(WinsockFdSet));
  uint32_t found = 0;
  for (uint32_t i = 0; set && i < set->count && i < WINSOCK_FD_SETSIZE; ++i) {
    found |= set->handles[i] == A(0);
  }
  ret_std(C, found, 2);
}

void ws2_report(void) {
  /* At zero as well: "the game never asked for networking" and "it asked and
     sent nothing" are different facts about a run. */
  x2_log_info("  ws2_32: %lu WSAStartup call(s), %lu WSACleanup; %u socket(s) "
              "open; %lu send(s), %lu receive(s); %lu untranslated request(s) "
              "refused\n",
              g_startups, g_cleanups, winsock_open_count(), g_sent, g_received,
              g_refused);
}
