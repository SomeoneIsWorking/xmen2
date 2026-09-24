#include "winsock_posix.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* A Winsock SOCKET is the host descriptor itself, recorded here when socket()
   made it; the table is what makes "not a socket" an answer rather than a
   write to whatever descriptor the number happens to name. */
enum { SOCKET_TABLE = 1024, SOCKET_OPEN = 1, SOCKET_NONBLOCKING = 2 };

static uint8_t g_sockets[SOCKET_TABLE];
static unsigned g_open;
static _Thread_local uint32_t g_last_error;

uint32_t winsock_error_from_errno(int err) {
  switch (err) {
  case EINTR:
    return WSAEINTR;
  case EBADF:
    return WSAEBADF;
  case EACCES:
  case EPERM:
    return WSAEACCES;
  case EFAULT:
    return WSAEFAULT;
  case EINVAL:
    return WSAEINVAL;
  case EMFILE:
  case ENFILE:
    return WSAEMFILE;
#if EAGAIN != EWOULDBLOCK
  case EAGAIN:
#endif
  case EWOULDBLOCK:
    return WSAEWOULDBLOCK;
  /* A non-blocking connect under way is WSAEWOULDBLOCK on Windows, not
     WSAEINPROGRESS, which there means a blocking call is already running. */
  case EINPROGRESS:
    return WSAEWOULDBLOCK;
  case EALREADY:
    return WSAEALREADY;
  case ENOTSOCK:
    return WSAENOTSOCK;
  case EDESTADDRREQ:
    return WSAEDESTADDRREQ;
  case EMSGSIZE:
    return WSAEMSGSIZE;
  case EPROTOTYPE:
    return WSAEPROTOTYPE;
  case ENOPROTOOPT:
    return WSAENOPROTOOPT;
  case EPROTONOSUPPORT:
    return WSAEPROTONOSUPPORT;
  case EOPNOTSUPP:
    return WSAEOPNOTSUPP;
  case EAFNOSUPPORT:
    return WSAEAFNOSUPPORT;
  case EADDRINUSE:
    return WSAEADDRINUSE;
  case EADDRNOTAVAIL:
    return WSAEADDRNOTAVAIL;
  case ENETDOWN:
    return WSAENETDOWN;
  case ENETUNREACH:
    return WSAENETUNREACH;
  case ECONNABORTED:
    return WSAECONNABORTED;
  case ECONNRESET:
  case EPIPE:
    return WSAECONNRESET;
  case ENOBUFS:
  case ENOMEM:
    return WSAENOBUFS;
  case EISCONN:
    return WSAEISCONN;
  case ENOTCONN:
    return WSAENOTCONN;
  case ETIMEDOUT:
    return WSAETIMEDOUT;
  case ECONNREFUSED:
    return WSAECONNREFUSED;
  case EHOSTUNREACH:
    return WSAEHOSTUNREACH;
  default:
    return WSAEINVAL;
  }
}

void winsock_set_last_error(uint32_t error) { g_last_error = error; }

uint32_t winsock_last_error(void) { return g_last_error; }

enum { WIN_AF_INET = 2, WIN_SOCKADDR_IN = 16 };

int winsock_sockaddr_to_host(const uint8_t *guest, int32_t length,
                             void *host_sockaddr_in, uint32_t *error) {
  struct sockaddr_in *out = host_sockaddr_in;
  if (!guest || length < WIN_SOCKADDR_IN) {
    *error = WSAEFAULT;
    return 0;
  }
  if ((guest[0] | guest[1] << 8) != WIN_AF_INET) {
    *error = WSAEAFNOSUPPORT;
    return 0;
  }
  memset(out, 0, sizeof *out);
  out->sin_family = AF_INET;
  memcpy(&out->sin_port, guest + 2, 2);
  memcpy(&out->sin_addr, guest + 4, 4);
  return 1;
}

void winsock_sockaddr_from_host(const void *host_sockaddr_in, uint8_t *guest) {
  const struct sockaddr_in *in = host_sockaddr_in;
  memset(guest, 0, WIN_SOCKADDR_IN);
  guest[0] = WIN_AF_INET;
  memcpy(guest + 2, &in->sin_port, 2);
  memcpy(guest + 4, &in->sin_addr, 4);
}

/* Winsock 2's numbering (ws2def.h / ws2ipdef.h). Only integer-valued options
   are here; a structured one such as SO_LINGER would need its own layout. */
typedef struct SockoptName {
  int32_t level;
  int32_t name;
  int host_level;
  int host_name;
} SockoptName;

static const SockoptName kSockopts[] = {
    {0xffff, 0x0004, SOL_SOCKET, SO_REUSEADDR},
    {0xffff, 0x0008, SOL_SOCKET, SO_KEEPALIVE},
    {0xffff, 0x0020, SOL_SOCKET, SO_BROADCAST},
    {0xffff, 0x1001, SOL_SOCKET, SO_SNDBUF},
    {0xffff, 0x1002, SOL_SOCKET, SO_RCVBUF},
    {6, 0x0001, IPPROTO_TCP, TCP_NODELAY},
    {0, 4, IPPROTO_IP, IP_TTL},
    {0, 10, IPPROTO_IP, IP_MULTICAST_TTL},
    {0, 11, IPPROTO_IP, IP_MULTICAST_LOOP},
};

int winsock_sockopt_to_host(int32_t level, int32_t name, int *host_level,
                            int *host_name) {
  for (size_t i = 0; i < sizeof kSockopts / sizeof kSockopts[0]; ++i) {
    if (kSockopts[i].level == level && kSockopts[i].name == name) {
      *host_level = kSockopts[i].host_level;
      *host_name = kSockopts[i].host_name;
      return 1;
    }
  }
  return 0;
}

static int table_index(uint32_t handle) {
  return handle < SOCKET_TABLE && (g_sockets[handle] & SOCKET_OPEN)
             ? (int)handle
             : -1;
}

int winsock_socket_open(int32_t family, int32_t type, int32_t protocol,
                        uint32_t *error) {
  if (family != WIN_AF_INET) {
    *error = WSAEAFNOSUPPORT;
    return -1;
  }
  /* SOCK_STREAM 1 / SOCK_DGRAM 2 and the IPPROTO numbers are the BSD values
     on both sides. */
  const int fd = socket(AF_INET,
                        type == 1   ? SOCK_STREAM
                        : type == 2 ? SOCK_DGRAM
                                    : -1,
                        protocol);
  if (fd < 0) {
    *error = winsock_error_from_errno(errno);
    return -1;
  }
  if (fd >= SOCKET_TABLE) {
    close(fd);
    *error = WSAEMFILE;
    return -1;
  }
#ifdef SO_NOSIGPIPE
  {
    const int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
  }
#endif
  g_sockets[fd] = SOCKET_OPEN;
  ++g_open;
  return fd;
}

int winsock_is_socket(uint32_t handle) { return table_index(handle) >= 0; }

int winsock_blocking(uint32_t handle) {
  const int fd = table_index(handle);
  return fd >= 0 && !(g_sockets[fd] & SOCKET_NONBLOCKING);
}

int winsock_set_blocking(uint32_t handle, int blocking, uint32_t *error) {
  const int fd = table_index(handle);
  if (fd < 0) {
    *error = WSAENOTSOCK;
    return 0;
  }
  const int flags = fcntl(fd, F_GETFL);
  if (flags < 0 ||
      fcntl(fd, F_SETFL, blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK) <
          0) {
    *error = winsock_error_from_errno(errno);
    return 0;
  }
  g_sockets[fd] =
      (uint8_t)(blocking ? SOCKET_OPEN : SOCKET_OPEN | SOCKET_NONBLOCKING);
  return 1;
}

int winsock_close(uint32_t handle, uint32_t *error) {
  const int fd = table_index(handle);
  if (fd < 0) {
    *error = WSAENOTSOCK;
    return 0;
  }
  g_sockets[fd] = 0;
  --g_open;
  if (close(fd) < 0) {
    *error = winsock_error_from_errno(errno);
    return 0;
  }
  return 1;
}

unsigned winsock_open_count(void) { return g_open; }

static int gather(const WinsockFdSet *set, short events, struct pollfd *polls,
                  int count, uint32_t *error) {
  if (!set) {
    return count;
  }
  if (set->count > WINSOCK_FD_SETSIZE) {
    *error = WSAEINVAL;
    return -1;
  }
  for (uint32_t i = 0; i < set->count; ++i) {
    const int fd = table_index(set->handles[i]);
    if (fd < 0) {
      *error = WSAENOTSOCK;
      return -1;
    }
    int at = 0;
    while (at < count && polls[at].fd != fd) {
      ++at;
    }
    if (at == count) {
      polls[count++] = (struct pollfd){fd, 0, 0};
    }
    polls[at].events |= events;
  }
  return count;
}

static int keep_ready(WinsockFdSet *set, short ready_on,
                      const struct pollfd *polls, int count) {
  uint32_t kept = 0;
  if (!set) {
    return 0;
  }
  for (uint32_t i = 0; i < set->count; ++i) {
    for (int at = 0; at < count; ++at) {
      if (polls[at].fd == (int)set->handles[i] &&
          (polls[at].revents & ready_on)) {
        set->handles[kept++] = set->handles[i];
        break;
      }
    }
  }
  set->count = kept;
  return (int)kept;
}

int winsock_select(WinsockFdSet *read, WinsockFdSet *write,
                   WinsockFdSet *except, int64_t timeout_us, uint32_t *error) {
  struct pollfd polls[WINSOCK_FD_SETSIZE * 3];
  int count = 0;
  count = gather(read, POLLIN, polls, count, error);
  count = count < 0 ? count : gather(write, POLLOUT, polls, count, error);
  count = count < 0 ? count : gather(except, POLLPRI, polls, count, error);
  if (count < 0) {
    return -1;
  }
  const int timeout_ms = timeout_us < 0 ? -1 : (int)((timeout_us + 999) / 1000);
  if (poll(polls, (nfds_t)count, timeout_ms) < 0) {
    *error = winsock_error_from_errno(errno);
    return -1;
  }
  /* A closed or failed peer is readable on Windows -- the recv that follows
     reports it -- and a failed connect lands in the except set. */
  return keep_ready(read, POLLIN | POLLHUP | POLLERR, polls, count) +
         keep_ready(write, POLLOUT, polls, count) +
         keep_ready(except, POLLPRI | POLLERR, polls, count);
}
