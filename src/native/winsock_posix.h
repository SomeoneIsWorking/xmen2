#ifndef X2_WINSOCK_POSIX_H
#define X2_WINSOCK_POSIX_H

/*
 * Winsock semantics over POSIX sockets, with no guest in sight.
 *
 * The WS2_32 import thunks (ws2_32.c) read the guest's arguments and hand
 * them here; everything that is a TRANSLATION -- an errno becoming a WSA code,
 * a Winsock option or ioctl becoming a host one, a Windows sockaddr_in or
 * fd_set becoming the host's, which socket is blocking -- lives in this one
 * module so it is tested as it ships, without a game.
 *
 * Only IPv4 exists here, because only IPv4 exists in the game: GameSpy's LAN
 * browse is a UDP broadcast and every address it carries is a 32-bit one.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WINSOCK_SOCKET_ERROR 0xffffffffu
#define WINSOCK_INVALID_SOCKET 0xffffffffu

enum {
  WSAEINTR = 10004,
  WSAEBADF = 10009,
  WSAEACCES = 10013,
  WSAEFAULT = 10014,
  WSAEINVAL = 10022,
  WSAEMFILE = 10024,
  WSAEWOULDBLOCK = 10035,
  WSAEINPROGRESS = 10036,
  WSAEALREADY = 10037,
  WSAENOTSOCK = 10038,
  WSAEDESTADDRREQ = 10039,
  WSAEMSGSIZE = 10040,
  WSAEPROTOTYPE = 10041,
  WSAENOPROTOOPT = 10042,
  WSAEPROTONOSUPPORT = 10043,
  WSAEOPNOTSUPP = 10045,
  WSAEAFNOSUPPORT = 10047,
  WSAEADDRINUSE = 10048,
  WSAEADDRNOTAVAIL = 10049,
  WSAENETDOWN = 10050,
  WSAENETUNREACH = 10051,
  WSAECONNABORTED = 10053,
  WSAECONNRESET = 10054,
  WSAENOBUFS = 10055,
  WSAEISCONN = 10056,
  WSAENOTCONN = 10057,
  WSAETIMEDOUT = 10060,
  WSAECONNREFUSED = 10061,
  WSAEHOSTUNREACH = 10065,
  WSASYSNOTREADY = 10091,
  WSAHOST_NOT_FOUND = 11001
};

/* The WSA code Win32 reports for a host errno; WSAEINVAL for one with no
   Winsock counterpart, which the caller also logs. */
uint32_t winsock_error_from_errno(int err);

/* Per calling thread, as WSAGetLastError is. */
void winsock_set_last_error(uint32_t error);
uint32_t winsock_last_error(void);

/* A Windows sockaddr_in (16 bytes: LE family, BE port, BE address) and the
   host's. Returns 0 with *error set for a family other than AF_INET or a
   length shorter than the structure. */
int winsock_sockaddr_to_host(const uint8_t *guest, int32_t length,
                             void *host_sockaddr_in, uint32_t *error);
void winsock_sockaddr_from_host(const void *host_sockaddr_in, uint8_t *guest);

/* setsockopt's (level, name) in Winsock numbering, as the host's. Returns 0
   for an option this layer does not translate. */
int winsock_sockopt_to_host(int32_t level, int32_t name, int *host_level,
                            int *host_name);

/* The sockets this process opened, and whether each blocks. A handle the
   game did not get from socket() is not a socket (WSAENOTSOCK). */
int winsock_socket_open(int32_t family, int32_t type, int32_t protocol,
                        uint32_t *error);
int winsock_is_socket(uint32_t handle);
int winsock_blocking(uint32_t handle);
int winsock_set_blocking(uint32_t handle, int blocking, uint32_t *error);
int winsock_close(uint32_t handle, uint32_t *error);

/* bind and getsockname as Winsock answers them. A datagram socket bound to
   one of the machine's adapter addresses receives that network's broadcasts
   on Windows but not on a POSIX host, where only a wildcard bind does; such a
   socket is bound to the wildcard and still reports the address it asked
   for. Anything else binds as given. */
int winsock_bind(uint32_t handle, const void *host_sockaddr_in,
                 uint32_t *error);
int winsock_getsockname(uint32_t handle, void *host_sockaddr_in,
                        uint32_t *error);

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

#ifdef __cplusplus
}
#endif

#endif /* X2_WINSOCK_POSIX_H */
