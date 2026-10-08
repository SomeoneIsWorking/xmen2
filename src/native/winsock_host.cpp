#include "winsock_host.h"

#include "platform_socket.h"
#include "winsock_resolve.h"

#include <mutex>
#include <string.h>

namespace {

/* Slot 0 stays empty so a zeroed handle is never a socket. */
enum {
  SOCKET_TABLE = 1024,
  SOCKET_OPEN = 1,
  SOCKET_NONBLOCKING = 2,
  SOCKET_DATAGRAM = 4
};

struct Slot {
  x2::native::Socket host;
  uint8_t flags;
  /* The adapter address a datagram socket asked to bind, which it is bound
     to in Winsock's eyes but not the host's (winsock_bind); 0 for none. */
  uint32_t bound_address;
};

Slot g_sockets[SOCKET_TABLE];
unsigned g_open;
thread_local uint32_t g_last_error;
std::once_flag g_host_start;
bool g_host_started;

/* The WSA code for the host call that just failed. */
uint32_t host_error() {
#if defined(_WIN32)
  return static_cast<uint32_t>(x2::native::socket_error());
#else
  return winsock_error_from_errno(x2::native::socket_error());
#endif
}

int fail(uint32_t *error, uint32_t code) {
  *error = code;
  return 0;
}

Slot *slot_of(uint32_t handle) {
  return handle != 0 && handle < SOCKET_TABLE &&
                 (g_sockets[handle].flags & SOCKET_OPEN)
             ? &g_sockets[handle]
             : nullptr;
}

} // namespace

#if !defined(_WIN32)
uint32_t winsock_error_from_errno(int err) {
  switch (err) {
  case EINTR:
    return WINSOCK_EINTR;
  case EBADF:
    return WINSOCK_EBADF;
  case EACCES:
  case EPERM:
    return WINSOCK_EACCES;
  case EFAULT:
    return WINSOCK_EFAULT;
  case EINVAL:
    return WINSOCK_EINVAL;
  case EMFILE:
  case ENFILE:
    return WINSOCK_EMFILE;
#if EAGAIN != EWOULDBLOCK
  case EAGAIN:
#endif
  case EWOULDBLOCK:
    return WINSOCK_EWOULDBLOCK;
  /* A non-blocking connect under way is WSAEWOULDBLOCK on Windows, not
     WSAEINPROGRESS, which there means a blocking call is already running. */
  case EINPROGRESS:
    return WINSOCK_EWOULDBLOCK;
  case EALREADY:
    return WINSOCK_EALREADY;
  case ENOTSOCK:
    return WINSOCK_ENOTSOCK;
  case EDESTADDRREQ:
    return WINSOCK_EDESTADDRREQ;
  case EMSGSIZE:
    return WINSOCK_EMSGSIZE;
  case EPROTOTYPE:
    return WINSOCK_EPROTOTYPE;
  case ENOPROTOOPT:
    return WINSOCK_ENOPROTOOPT;
  case EPROTONOSUPPORT:
    return WINSOCK_EPROTONOSUPPORT;
  case EOPNOTSUPP:
    return WINSOCK_EOPNOTSUPP;
  case EAFNOSUPPORT:
    return WINSOCK_EAFNOSUPPORT;
  case EADDRINUSE:
    return WINSOCK_EADDRINUSE;
  case EADDRNOTAVAIL:
    return WINSOCK_EADDRNOTAVAIL;
  case ENETDOWN:
    return WINSOCK_ENETDOWN;
  case ENETUNREACH:
    return WINSOCK_ENETUNREACH;
  case ECONNABORTED:
    return WINSOCK_ECONNABORTED;
  case ECONNRESET:
  case EPIPE:
    return WINSOCK_ECONNRESET;
  case ENOBUFS:
  case ENOMEM:
    return WINSOCK_ENOBUFS;
  case EISCONN:
    return WINSOCK_EISCONN;
  case ENOTCONN:
    return WINSOCK_ENOTCONN;
  case ETIMEDOUT:
    return WINSOCK_ETIMEDOUT;
  case ECONNREFUSED:
    return WINSOCK_ECONNREFUSED;
  case EHOSTUNREACH:
    return WINSOCK_EHOSTUNREACH;
  default:
    return WINSOCK_EINVAL;
  }
}
#endif

int winsock_host_ready(void) {
  std::call_once(g_host_start,
                 [] { g_host_started = x2::native::socket_startup() != 0; });
  return g_host_started;
}

void winsock_set_last_error(uint32_t error) { g_last_error = error; }

uint32_t winsock_last_error(void) { return g_last_error; }

enum { WIN_AF_INET = 2, WIN_SOCKADDR_IN = 16 };

int winsock_sockaddr_to_host(const uint8_t *guest, int32_t length,
                             void *host_sockaddr_in, uint32_t *error) {
  struct sockaddr_in *out = static_cast<struct sockaddr_in *>(host_sockaddr_in);
  if (!guest || length < WIN_SOCKADDR_IN) {
    return fail(error, WINSOCK_EFAULT);
  }
  if ((guest[0] | guest[1] << 8) != WIN_AF_INET) {
    return fail(error, WINSOCK_EAFNOSUPPORT);
  }
  memset(out, 0, sizeof *out);
  out->sin_family = AF_INET;
  memcpy(&out->sin_port, guest + 2, 2);
  memcpy(&out->sin_addr, guest + 4, 4);
  return 1;
}

void winsock_sockaddr_from_host(const void *host_sockaddr_in, uint8_t *guest) {
  const struct sockaddr_in *in =
      static_cast<const struct sockaddr_in *>(host_sockaddr_in);
  memset(guest, 0, WIN_SOCKADDR_IN);
  guest[0] = WIN_AF_INET;
  memcpy(guest + 2, &in->sin_port, 2);
  memcpy(guest + 4, &in->sin_addr, 4);
}

namespace {

/* One part of a dotted address; `*text` is left after its digits. */
int parse_part(const char **text, uint32_t *out) {
  const char *at = *text;
  uint32_t base = 10;
  if (at[0] == '0' && (at[1] == 'x' || at[1] == 'X')) {
    base = 16;
    at += 2;
  } else if (at[0] == '0') {
    base = 8;
  }
  uint64_t value = 0;
  const char *digits = at;
  for (;; ++at) {
    const char c = *at;
    uint32_t digit = 0;
    if (c >= '0' && c <= '9') {
      digit = static_cast<uint32_t>(c - '0');
    } else if (base == 16 && c >= 'a' && c <= 'f') {
      digit = static_cast<uint32_t>(c - 'a' + 10);
    } else if (base == 16 && c >= 'A' && c <= 'F') {
      digit = static_cast<uint32_t>(c - 'A' + 10);
    } else {
      break;
    }
    if (digit >= base) {
      return 0;
    }
    value = value * base + digit;
    if (value > 0xffffffffu) {
      return 0;
    }
  }
  if (at == digits && base != 8) {
    return 0;
  }
  *out = static_cast<uint32_t>(value);
  *text = at;
  return 1;
}

} // namespace

int winsock_parse_ipv4(const char *text, uint32_t *network_order) {
  uint32_t parts[4];
  unsigned count = 0;
  for (;;) {
    if (count == 4 || !parse_part(&text, &parts[count])) {
      return 0;
    }
    ++count;
    if (*text != '.') {
      break;
    }
    ++text;
  }
  if (*text != 0 && *text != ' ' && *text != '\t' && *text != '\n') {
    return 0;
  }
  uint32_t host = parts[count - 1];
  const uint32_t last_limit = count == 1   ? 0xffffffffu
                              : count == 2 ? 0xffffffu
                              : count == 3 ? 0xffffu
                                           : 0xffu;
  if (host > last_limit) {
    return 0;
  }
  for (unsigned i = 0; i + 1 < count; ++i) {
    if (parts[i] > 0xffu) {
      return 0;
    }
    host |= parts[i] << (24 - 8 * i);
  }
  *network_order = htonl(host);
  return 1;
}

namespace {

/* Winsock 2's numbering (ws2def.h / ws2ipdef.h). Only integer-valued options
   are here; a structured one such as SO_LINGER would need its own layout. */
struct SockoptName {
  int32_t level;
  int32_t name;
  int host_level;
  int host_name;
};

const SockoptName kSockopts[] = {
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

} // namespace

int winsock_sockopt_to_host(int32_t level, int32_t name, int *host_level,
                            int *host_name) {
  for (const SockoptName &option : kSockopts) {
    if (option.level == level && option.name == name) {
      *host_level = option.host_level;
      *host_name = option.host_name;
      return 1;
    }
  }
  return 0;
}

int winsock_socket_open(int32_t family, int32_t type, int32_t protocol,
                        uint32_t *error) {
  if (!winsock_host_ready()) {
    *error = WINSOCK_ENETDOWN;
    return -1;
  }
  if (family != WIN_AF_INET) {
    *error = WINSOCK_EAFNOSUPPORT;
    return -1;
  }
  uint32_t handle = 1;
  while (handle < SOCKET_TABLE && (g_sockets[handle].flags & SOCKET_OPEN)) {
    ++handle;
  }
  if (handle == SOCKET_TABLE) {
    *error = WINSOCK_EMFILE;
    return -1;
  }
  /* SOCK_STREAM 1 / SOCK_DGRAM 2 and the IPPROTO numbers are the BSD values
     on both sides. */
  const x2::native::Socket host =
      x2::native::socket_open(AF_INET,
                              type == 1   ? SOCK_STREAM
                              : type == 2 ? SOCK_DGRAM
                                          : -1,
                              protocol);
  if (x2::native::socket_is_invalid(host)) {
    *error = host_error();
    return -1;
  }
#ifdef SO_NOSIGPIPE
  (void)x2::native::socket_set_int_option(host, SOL_SOCKET, SO_NOSIGPIPE, 1);
#endif
  g_sockets[handle] =
      Slot{host,
           static_cast<uint8_t>(type == 2 ? SOCKET_OPEN | SOCKET_DATAGRAM
                                          : SOCKET_OPEN),
           0};
  ++g_open;
  return static_cast<int>(handle);
}

int winsock_is_socket(uint32_t handle) { return slot_of(handle) != nullptr; }

int winsock_blocking(uint32_t handle) {
  const Slot *slot = slot_of(handle);
  return slot && !(slot->flags & SOCKET_NONBLOCKING);
}

int winsock_set_blocking(uint32_t handle, int blocking, uint32_t *error) {
  Slot *slot = slot_of(handle);
  if (!slot) {
    return fail(error, WINSOCK_ENOTSOCK);
  }
  if (x2::native::socket_set_nonblocking(slot->host, !blocking) != 0) {
    return fail(error, host_error());
  }
  slot->flags =
      static_cast<uint8_t>(blocking ? slot->flags & ~SOCKET_NONBLOCKING
                                    : slot->flags | SOCKET_NONBLOCKING);
  return 1;
}

int winsock_close(uint32_t handle, uint32_t *error) {
  Slot *slot = slot_of(handle);
  if (!slot) {
    return fail(error, WINSOCK_ENOTSOCK);
  }
  const x2::native::Socket host = slot->host;
  *slot = Slot{x2::native::kSocketInvalid, 0, 0};
  --g_open;
  if (x2::native::socket_close(host) != 0) {
    return fail(error, host_error());
  }
  return 1;
}

unsigned winsock_open_count(void) { return g_open; }

namespace {

#if !defined(_WIN32)
int is_adapter_address(uint32_t network_order) {
  uint32_t local[WINSOCK_HOST_ADDRESSES];
  const unsigned count = winsock_local_addresses(local, WINSOCK_HOST_ADDRESSES);
  for (unsigned i = 0; i < count; ++i) {
    if (local[i] == network_order) {
      return 1;
    }
  }
  return 0;
}
#endif

/* Whether a bind to this address must widen to the wildcard to hear the
   broadcasts Windows delivers to it. */
int widens(const Slot *slot, uint32_t network_order) {
#if defined(_WIN32)
  (void)slot;
  (void)network_order;
  return 0;
#else
  return (slot->flags & SOCKET_DATAGRAM) && is_adapter_address(network_order);
#endif
}

} // namespace

int winsock_bind(uint32_t handle, const void *host_sockaddr_in,
                 uint32_t *error) {
  Slot *slot = slot_of(handle);
  if (!slot) {
    return fail(error, WINSOCK_ENOTSOCK);
  }
  struct sockaddr_in at;
  memcpy(&at, host_sockaddr_in, sizeof at);
  const uint32_t requested = at.sin_addr.s_addr;
  const int widen = widens(slot, requested);
  if (widen) {
    at.sin_addr.s_addr = htonl(INADDR_ANY);
  }
  if (x2::native::socket_bind(slot->host,
                              reinterpret_cast<const struct sockaddr *>(&at),
                              sizeof at) != 0) {
    return fail(error, host_error());
  }
  slot->bound_address = widen ? requested : 0;
  return 1;
}

int winsock_getsockname(uint32_t handle, void *host_sockaddr_in,
                        uint32_t *error) {
  const Slot *slot = slot_of(handle);
  struct sockaddr_in at;
  if (!slot) {
    return fail(error, WINSOCK_ENOTSOCK);
  }
  if (x2::native::socket_name(slot->host, &at) != 0) {
    return fail(error, host_error());
  }
  if (slot->bound_address) {
    at.sin_addr.s_addr = slot->bound_address;
  }
  memcpy(host_sockaddr_in, &at, sizeof at);
  return 1;
}

int winsock_connect(uint32_t handle, const void *host_sockaddr_in,
                    uint32_t *error) {
  const Slot *slot = slot_of(handle);
  if (!slot) {
    return fail(error, WINSOCK_ENOTSOCK);
  }
  struct sockaddr_in to;
  memcpy(&to, host_sockaddr_in, sizeof to);
  return x2::native::socket_connect(slot->host, &to) == 0
             ? 1
             : fail(error, host_error());
}

int winsock_shutdown(uint32_t handle, int how, uint32_t *error) {
  const Slot *slot = slot_of(handle);
  if (!slot) {
    return fail(error, WINSOCK_ENOTSOCK);
  }
  return x2::native::socket_shutdown(slot->host, how) == 0
             ? 1
             : fail(error, host_error());
}

int winsock_pending(uint32_t handle, uint32_t *bytes, uint32_t *error) {
  const Slot *slot = slot_of(handle);
  unsigned pending = 0;
  if (!slot) {
    return fail(error, WINSOCK_ENOTSOCK);
  }
  if (x2::native::socket_pending(slot->host, &pending) != 0) {
    return fail(error, host_error());
  }
  *bytes = pending;
  return 1;
}

int winsock_set_option(uint32_t handle, int host_level, int host_name,
                       int value, uint32_t *error) {
  const Slot *slot = slot_of(handle);
  if (!slot) {
    return fail(error, WINSOCK_ENOTSOCK);
  }
  return x2::native::socket_set_int_option(slot->host, host_level, host_name,
                                           value) == 0
             ? 1
             : fail(error, host_error());
}

namespace {

int64_t transferred(x2::native::SocketSsize n, uint32_t *error) {
  if (n < 0) {
    *error = host_error();
    return -1;
  }
  return static_cast<int64_t>(n);
}

} // namespace

int64_t winsock_send(uint32_t handle, const void *data, size_t size, int flags,
                     uint32_t *error) {
  const Slot *slot = slot_of(handle);
  if (!slot) {
    *error = WINSOCK_ENOTSOCK;
    return -1;
  }
  return transferred(
      x2::native::socket_send_flags(slot->host, data, size, flags), error);
}

int64_t winsock_recv(uint32_t handle, void *data, size_t size, int flags,
                     uint32_t *error) {
  const Slot *slot = slot_of(handle);
  if (!slot) {
    *error = WINSOCK_ENOTSOCK;
    return -1;
  }
  return transferred(
      x2::native::socket_recv_flags(slot->host, data, size, flags), error);
}

int64_t winsock_sendto(uint32_t handle, const void *data, size_t size,
                       int flags, const void *host_sockaddr_in,
                       uint32_t *error) {
  const Slot *slot = slot_of(handle);
  if (!slot) {
    *error = WINSOCK_ENOTSOCK;
    return -1;
  }
  struct sockaddr_in to;
  memcpy(&to, host_sockaddr_in, sizeof to);
  return transferred(
      x2::native::socket_sendto(slot->host, data, size, flags, &to), error);
}

int64_t winsock_recvfrom(uint32_t handle, void *data, size_t size, int flags,
                         void *host_sockaddr_in, uint32_t *error) {
  const Slot *slot = slot_of(handle);
  if (!slot) {
    *error = WINSOCK_ENOTSOCK;
    return -1;
  }
  struct sockaddr_in from;
  memset(&from, 0, sizeof from);
  const int64_t n = transferred(
      x2::native::socket_recvfrom(slot->host, data, size, flags, &from), error);
  if (n >= 0 && host_sockaddr_in) {
    memcpy(host_sockaddr_in, &from, sizeof from);
  }
  return n;
}

namespace {

/* One host poll entry per distinct guest handle across the three sets. */
struct PollSet {
  x2::native::PollFd polls[WINSOCK_FD_SETSIZE * 3];
  uint32_t handles[WINSOCK_FD_SETSIZE * 3];
  unsigned count;
};

int gather(const WinsockFdSet *set, short events, PollSet *poll,
           uint32_t *error) {
  if (!set) {
    return 1;
  }
  if (set->count > WINSOCK_FD_SETSIZE) {
    return fail(error, WINSOCK_EINVAL);
  }
  for (uint32_t i = 0; i < set->count; ++i) {
    const Slot *slot = slot_of(set->handles[i]);
    if (!slot) {
      return fail(error, WINSOCK_ENOTSOCK);
    }
    unsigned at = 0;
    while (at < poll->count && poll->handles[at] != set->handles[i]) {
      ++at;
    }
    if (at == poll->count) {
      x2::native::PollFd entry;
      memset(&entry, 0, sizeof entry);
      entry.fd = slot->host;
      poll->polls[at] = entry;
      poll->handles[at] = set->handles[i];
      ++poll->count;
    }
    poll->polls[at].events =
        static_cast<short>(poll->polls[at].events | events);
  }
  return 1;
}

int keep_ready(WinsockFdSet *set, int ready_on, const PollSet *poll) {
  uint32_t kept = 0;
  if (!set) {
    return 0;
  }
  for (uint32_t i = 0; i < set->count; ++i) {
    for (unsigned at = 0; at < poll->count; ++at) {
      if (poll->handles[at] == set->handles[i] &&
          (poll->polls[at].revents & ready_on)) {
        set->handles[kept++] = set->handles[i];
        break;
      }
    }
  }
  set->count = kept;
  return static_cast<int>(kept);
}

} // namespace

int winsock_select(WinsockFdSet *read, WinsockFdSet *write,
                   WinsockFdSet *except, int64_t timeout_us, uint32_t *error) {
  PollSet poll;
  poll.count = 0;
  if (!gather(read, x2::native::kPollIn, &poll, error) ||
      !gather(write, x2::native::kPollOut, &poll, error) ||
      !gather(except, x2::native::kPollExcept, &poll, error)) {
    return -1;
  }
  const int timeout_ms =
      timeout_us < 0 ? -1 : static_cast<int>((timeout_us + 999) / 1000);
  if (x2::native::socket_poll(poll.polls, poll.count, timeout_ms) < 0) {
    *error = host_error();
    return -1;
  }
  /* A closed or failed peer is readable on Windows -- the recv that follows
     reports it -- and a failed connect lands in the except set. */
  return keep_ready(read,
                    x2::native::kPollIn | x2::native::kPollHup |
                        x2::native::kPollErr,
                    &poll) +
         keep_ready(write, x2::native::kPollOut, &poll) +
         keep_ready(except, x2::native::kPollExcept | x2::native::kPollErr,
                    &poll);
}
