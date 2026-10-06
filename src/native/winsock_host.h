#ifndef X2_WINSOCK_HOST_H
#define X2_WINSOCK_HOST_H

/*
 * Winsock semantics over the host's sockets, with no guest in sight.
 *
 * The WS2_32 import thunks (ws2_32.cpp) read the guest's arguments and hand
 * them here; everything that is a TRANSLATION -- a host error becoming a WSA
 * code, a Winsock option becoming a host one, a Windows sockaddr_in or fd_set
 * becoming the host's, which socket is blocking -- lives in this one module
 * so it is tested as it ships, without a game. The host socket itself goes
 * through platform_socket.h, so this module is the same on POSIX and Windows.
 *
 * A guest SOCKET is a slot in this module's table, never a host descriptor:
 * the table is what makes "not a socket" an answer rather than a write to
 * whatever descriptor the number happens to name.
 *
 * Only IPv4 exists here, because only IPv4 exists in the game: GameSpy's LAN
 * browse is a UDP broadcast and every address it carries is a 32-bit one.
 *
 * `host_sockaddr_in` arguments are the host's struct sockaddr_in.
 */

#include <stddef.h>
#include <stdint.h>

#define WINSOCK_SOCKET_ERROR 0xffffffffu
#define WINSOCK_INVALID_SOCKET 0xffffffffu

/* The guest's WSA codes; Win32 ABI values, whatever the host is. */
enum {
  WINSOCK_EINTR = 10004,
  WINSOCK_EBADF = 10009,
  WINSOCK_EACCES = 10013,
  WINSOCK_EFAULT = 10014,
  WINSOCK_EINVAL = 10022,
  WINSOCK_EMFILE = 10024,
  WINSOCK_EWOULDBLOCK = 10035,
  WINSOCK_EINPROGRESS = 10036,
  WINSOCK_EALREADY = 10037,
  WINSOCK_ENOTSOCK = 10038,
  WINSOCK_EDESTADDRREQ = 10039,
  WINSOCK_EMSGSIZE = 10040,
  WINSOCK_EPROTOTYPE = 10041,
  WINSOCK_ENOPROTOOPT = 10042,
  WINSOCK_EPROTONOSUPPORT = 10043,
  WINSOCK_EOPNOTSUPP = 10045,
  WINSOCK_EAFNOSUPPORT = 10047,
  WINSOCK_EADDRINUSE = 10048,
  WINSOCK_EADDRNOTAVAIL = 10049,
  WINSOCK_ENETDOWN = 10050,
  WINSOCK_ENETUNREACH = 10051,
  WINSOCK_ECONNABORTED = 10053,
  WINSOCK_ECONNRESET = 10054,
  WINSOCK_ENOBUFS = 10055,
  WINSOCK_EISCONN = 10056,
  WINSOCK_ENOTCONN = 10057,
  WINSOCK_ETIMEDOUT = 10060,
  WINSOCK_ECONNREFUSED = 10061,
  WINSOCK_EHOSTUNREACH = 10065,
  WINSOCK_SYSNOTREADY = 10091,
  WINSOCK_HOST_NOT_FOUND = 11001
};

#if !defined(_WIN32)
/* The WSA code Win32 reports for a host errno; WINSOCK_EINVAL for one with
   no Winsock counterpart. A Windows host's codes are already WSA codes. */
uint32_t winsock_error_from_errno(int err);
#endif

/* Starts the host's socket layer once for the process; 0 when the host has
   none (WSAStartup failed). Every call that touches a host socket or name
   asks first. */
int winsock_host_ready(void);

/* Per calling thread, as WSAGetLastError is. */
void winsock_set_last_error(uint32_t error);
uint32_t winsock_last_error(void);

/* A Windows sockaddr_in (16 bytes: LE family, BE port, BE address) and the
   host's. Returns 0 with *error set for a family other than AF_INET or a
   length shorter than the structure. */
int winsock_sockaddr_to_host(const uint8_t *guest, int32_t length,
                             void *host_sockaddr_in, uint32_t *error);
void winsock_sockaddr_from_host(const void *host_sockaddr_in, uint8_t *guest);

/* inet_addr's text forms: a, a.b, a.b.c or a.b.c.d, each part decimal, 0x
   hex or 0 octal, the last part filling the remaining bytes. Returns 1 with
   the address in network order, or 0 for a malformed one. */
int winsock_parse_ipv4(const char *text, uint32_t *network_order);

/* setsockopt's (level, name) in Winsock numbering, as the host's. Returns 0
   for an option this layer does not translate. */
int winsock_sockopt_to_host(int32_t level, int32_t name, int *host_level,
                            int *host_name);

/* The sockets this process opened, and whether each blocks. A handle the
   game did not get from socket() is not a socket (WINSOCK_ENOTSOCK). Returns
   the new handle, or -1 with *error set. */
int winsock_socket_open(int32_t family, int32_t type, int32_t protocol,
                        uint32_t *error);
int winsock_is_socket(uint32_t handle);
int winsock_blocking(uint32_t handle);
int winsock_set_blocking(uint32_t handle, int blocking, uint32_t *error);
int winsock_close(uint32_t handle, uint32_t *error);

/* bind and getsockname as Winsock answers them. A datagram socket bound to
   one of the machine's adapter addresses receives that network's broadcasts
   on Windows but not on a POSIX host, where only a wildcard bind does; there
   such a socket is bound to the wildcard and still reports the address it
   asked for. Anything else binds as given. */
int winsock_bind(uint32_t handle, const void *host_sockaddr_in,
                 uint32_t *error);
int winsock_getsockname(uint32_t handle, void *host_sockaddr_in,
                        uint32_t *error);

/* The rest of the socket calls, each 1 on success or 0 with *error set.
   `how` is SD_RECEIVE/SD_SEND/SD_BOTH; `host_level`/`host_name` come from
   winsock_sockopt_to_host. */
int winsock_connect(uint32_t handle, const void *host_sockaddr_in,
                    uint32_t *error);
int winsock_shutdown(uint32_t handle, int how, uint32_t *error);
int winsock_pending(uint32_t handle, uint32_t *bytes, uint32_t *error);
int winsock_set_option(uint32_t handle, int host_level, int host_name,
                       int value, uint32_t *error);

/* Data calls with Winsock's MSG_OOB/PEEK/DONTROUTE flags, which are the
   host's. Return the byte count, or -1 with *error set. recvfrom's
   `host_sockaddr_in` receives the sender and may be NULL. */
int64_t winsock_send(uint32_t handle, const void *data, size_t size, int flags,
                     uint32_t *error);
int64_t winsock_recv(uint32_t handle, void *data, size_t size, int flags,
                     uint32_t *error);
int64_t winsock_sendto(uint32_t handle, const void *data, size_t size,
                       int flags, const void *host_sockaddr_in,
                       uint32_t *error);
int64_t winsock_recvfrom(uint32_t handle, void *data, size_t size, int flags,
                         void *host_sockaddr_in, uint32_t *error);

/* select over a Windows fd_set triple (u32 count, u32 handles[64]) given as
   host-addressable arrays; each set is rewritten to hold only its ready
   handles. timeout_us < 0 waits forever. Returns the ready total or -1 with
   *error set. */
#define WINSOCK_FD_SETSIZE 64
typedef struct WinsockFdSet {
  uint32_t count;
  uint32_t handles[WINSOCK_FD_SETSIZE];
} WinsockFdSet;
int winsock_select(WinsockFdSet *read, WinsockFdSet *write,
                   WinsockFdSet *except, int64_t timeout_us, uint32_t *error);

/* How many sockets are open, for the shutdown report. */
unsigned winsock_open_count(void);

#endif /* X2_WINSOCK_HOST_H */
