#ifndef X2_PLATFORM_SOCKET_H
#define X2_PLATFORM_SOCKET_H

/* One host socket contract for the live control channel, the guest's Winsock
 * (winsock_host.cpp) and the LAN presence socket. Callers receive an opaque
 * descriptor and the host's own error code, and never depend on POSIX fd
 * width or Winsock lifetime. Every address is IPv4. */
#if defined(_WIN32)
#include <errno.h>
#include <stddef.h>
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET x2_socket_t;
typedef int x2_socket_ssize_t;
typedef WSAPOLLFD x2_pollfd;
#define X2_SOCKET_INVALID INVALID_SOCKET
/* WSAPoll refuses POLLPRI; a failed socket reports POLLERR without asking. */
#define X2_POLL_IN POLLIN
#define X2_POLL_OUT POLLOUT
#define X2_POLL_EXCEPT 0
#define X2_POLL_HUP POLLHUP
#define X2_POLL_ERR POLLERR

static inline int x2_socket_startup(void) {
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

static inline int x2_socket_is_invalid(x2_socket_t socket) {
  return socket == X2_SOCKET_INVALID;
}

/* The host's code for the last failure: a WSA code here, errno on POSIX. */
static inline int x2_socket_error(void) { return WSAGetLastError(); }

static inline int x2_socket_error_is_interrupt(void) {
  return WSAGetLastError() == WSAEINTR;
}

static inline int x2_socket_error_would_block(int error) {
  return error == WSAEWOULDBLOCK;
}

static inline x2_socket_t x2_socket_open(int domain, int type, int protocol) {
  return socket(domain, type, protocol);
}

static inline x2_socket_t x2_socket_accept(x2_socket_t socket) {
  return accept(socket, NULL, NULL);
}

static inline int x2_socket_bind(x2_socket_t socket,
                                 const struct sockaddr *address,
                                 int address_size) {
  return bind(socket, address, address_size);
}

static inline int x2_socket_listen(x2_socket_t socket, int backlog) {
  return listen(socket, backlog);
}

static inline int x2_socket_connect(x2_socket_t socket,
                                    const struct sockaddr_in *to) {
  return connect(socket, (const struct sockaddr *)to, (int)sizeof *to);
}

/* SD_RECEIVE/SD_SEND/SD_BOTH, which are SHUT_RD/WR/RDWR's values. */
static inline int x2_socket_shutdown(x2_socket_t socket, int how) {
  return shutdown(socket, how);
}

static inline int x2_socket_reuse_address(x2_socket_t socket) {
  const char enabled = 1;
  return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled,
                    (int)sizeof enabled);
}

static inline int x2_socket_set_int_option(x2_socket_t socket, int level,
                                           int name, int value) {
  return setsockopt(socket, level, name, (const char *)&value,
                    (int)sizeof value);
}

static inline int x2_socket_set_nonblocking(x2_socket_t socket,
                                            int nonblocking) {
  u_long mode = nonblocking ? 1u : 0u;
  return ioctlsocket(socket, FIONBIO, &mode);
}

/* Bytes waiting to be read. */
static inline int x2_socket_pending(x2_socket_t socket, unsigned *bytes) {
  u_long pending = 0;
  const int rc = ioctlsocket(socket, FIONREAD, &pending);
  *bytes = (unsigned)pending;
  return rc;
}

static inline int x2_socket_name(x2_socket_t socket, struct sockaddr_in *out) {
  int size = (int)sizeof *out;
  return getsockname(socket, (struct sockaddr *)out, &size);
}

static inline x2_socket_ssize_t
x2_socket_send(x2_socket_t socket, const void *buffer, size_t size) {
  return send(socket, (const char *)buffer, (int)size, 0);
}

static inline x2_socket_ssize_t x2_socket_recv(x2_socket_t socket, void *buffer,
                                               size_t size) {
  return recv(socket, (char *)buffer, (int)size, 0);
}

static inline x2_socket_ssize_t x2_socket_send_flags(x2_socket_t socket,
                                                     const void *buffer,
                                                     size_t size, int flags) {
  return send(socket, (const char *)buffer, (int)size, flags);
}

static inline x2_socket_ssize_t
x2_socket_recv_flags(x2_socket_t socket, void *buffer, size_t size, int flags) {
  return recv(socket, (char *)buffer, (int)size, flags);
}

static inline x2_socket_ssize_t x2_socket_sendto(x2_socket_t socket,
                                                 const void *buffer,
                                                 size_t size, int flags,
                                                 const struct sockaddr_in *to) {
  return sendto(socket, (const char *)buffer, (int)size, flags,
                (const struct sockaddr *)to, (int)sizeof *to);
}

static inline x2_socket_ssize_t x2_socket_recvfrom(x2_socket_t socket,
                                                   void *buffer, size_t size,
                                                   int flags,
                                                   struct sockaddr_in *from) {
  int length = (int)sizeof *from;
  return recvfrom(socket, (char *)buffer, (int)size, flags,
                  (struct sockaddr *)from, &length);
}

/* The size of the next datagram, copied into `buffer` and truncated to it.
 * Winsock reports a truncated datagram as WSAEMSGSIZE and cannot say how
 * long it was, so it is answered as one byte longer than the buffer. */
static inline x2_socket_ssize_t
x2_socket_recv_datagram(x2_socket_t socket, void *buffer, size_t size) {
  const int received = recv(socket, (char *)buffer, (int)size, 0);
  if (received < 0 && WSAGetLastError() == WSAEMSGSIZE)
    return (x2_socket_ssize_t)size + 1;
  return received;
}

/* poll(2)'s contract, including an empty set waiting out the timeout, which
 * WSAPoll refuses. */
static inline int x2_socket_poll(x2_pollfd *sockets, unsigned count,
                                 int timeout_ms) {
  if (count == 0) {
    Sleep(timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
    return 0;
  }
  return WSAPoll(sockets, count, timeout_ms);
}

static inline int x2_socket_host_name(char *out, size_t size) {
  return gethostname(out, (int)size);
}

static inline int x2_socket_close(x2_socket_t socket) {
  return closesocket(socket);
}
#else
#include "platform_posix.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int x2_socket_t;
typedef ssize_t x2_socket_ssize_t;
typedef struct pollfd x2_pollfd;
#define X2_SOCKET_INVALID (-1)
#define X2_POLL_IN POLLIN
#define X2_POLL_OUT POLLOUT
#define X2_POLL_EXCEPT POLLPRI
#define X2_POLL_HUP POLLHUP
#define X2_POLL_ERR POLLERR

/* A write to a closed peer is an error return, never SIGPIPE; Darwin has
   SO_NOSIGPIPE instead. */
#ifdef MSG_NOSIGNAL
#define X2_SOCKET_SEND_FLAGS MSG_NOSIGNAL
#else
#define X2_SOCKET_SEND_FLAGS 0
#endif

static inline int x2_socket_startup(void) { return 1; }

static inline int x2_socket_is_invalid(x2_socket_t socket) {
  return socket < 0;
}

static inline int x2_socket_error(void) { return errno; }

static inline int x2_socket_error_is_interrupt(void) { return errno == EINTR; }

static inline int x2_socket_error_would_block(int error) {
  return error == EAGAIN || error == EWOULDBLOCK;
}

static inline x2_socket_t x2_socket_open(int domain, int type, int protocol) {
  return socket(domain, type, protocol);
}

static inline x2_socket_t x2_socket_accept(x2_socket_t socket) {
  return accept(socket, NULL, NULL);
}

static inline int x2_socket_bind(x2_socket_t socket,
                                 const struct sockaddr *address,
                                 socklen_t address_size) {
  return bind(socket, address, address_size);
}

static inline int x2_socket_listen(x2_socket_t socket, int backlog) {
  return listen(socket, backlog);
}

static inline int x2_socket_connect(x2_socket_t socket,
                                    const struct sockaddr_in *to) {
  return connect(socket, (const struct sockaddr *)to, sizeof *to);
}

static inline int x2_socket_shutdown(x2_socket_t socket, int how) {
  return shutdown(socket, how);
}

static inline int x2_socket_reuse_address(x2_socket_t socket) {
  const int enabled = 1;
  return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled);
}

static inline int x2_socket_set_int_option(x2_socket_t socket, int level,
                                           int name, int value) {
  return setsockopt(socket, level, name, &value, sizeof value);
}

static inline int x2_socket_set_nonblocking(x2_socket_t socket,
                                            int nonblocking) {
  const int flags = fcntl(socket, F_GETFL);
  if (flags < 0)
    return -1;
  return fcntl(socket, F_SETFL,
               nonblocking ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
}

static inline int x2_socket_pending(x2_socket_t socket, unsigned *bytes) {
  int pending = 0;
  const int rc = ioctl(socket, FIONREAD, &pending);
  *bytes = (unsigned)pending;
  return rc;
}

static inline int x2_socket_name(x2_socket_t socket, struct sockaddr_in *out) {
  socklen_t size = sizeof *out;
  return getsockname(socket, (struct sockaddr *)out, &size);
}

static inline x2_socket_ssize_t
x2_socket_send(x2_socket_t socket, const void *buffer, size_t size) {
  return send(socket, buffer, size, 0);
}

static inline x2_socket_ssize_t x2_socket_recv(x2_socket_t socket, void *buffer,
                                               size_t size) {
  return recv(socket, buffer, size, 0);
}

static inline x2_socket_ssize_t x2_socket_send_flags(x2_socket_t socket,
                                                     const void *buffer,
                                                     size_t size, int flags) {
  return send(socket, buffer, size, flags | X2_SOCKET_SEND_FLAGS);
}

static inline x2_socket_ssize_t
x2_socket_recv_flags(x2_socket_t socket, void *buffer, size_t size, int flags) {
  return recv(socket, buffer, size, flags);
}

static inline x2_socket_ssize_t x2_socket_sendto(x2_socket_t socket,
                                                 const void *buffer,
                                                 size_t size, int flags,
                                                 const struct sockaddr_in *to) {
  return sendto(socket, buffer, size, flags | X2_SOCKET_SEND_FLAGS,
                (const struct sockaddr *)to, sizeof *to);
}

static inline x2_socket_ssize_t x2_socket_recvfrom(x2_socket_t socket,
                                                   void *buffer, size_t size,
                                                   int flags,
                                                   struct sockaddr_in *from) {
  socklen_t length = sizeof *from;
  return recvfrom(socket, buffer, size, flags, (struct sockaddr *)from,
                  &length);
}

/* The size of the next datagram, copied into `buffer` and truncated to it. */
static inline x2_socket_ssize_t
x2_socket_recv_datagram(x2_socket_t socket, void *buffer, size_t size) {
#ifdef MSG_TRUNC
  return recv(socket, buffer, size, MSG_TRUNC);
#else
  return recv(socket, buffer, size, 0);
#endif
}

static inline int x2_socket_poll(x2_pollfd *sockets, unsigned count,
                                 int timeout_ms) {
  return poll(sockets, (nfds_t)count, timeout_ms);
}

static inline int x2_socket_host_name(char *out, size_t size) {
  return gethostname(out, size);
}

static inline int x2_socket_close(x2_socket_t socket) { return close(socket); }
#endif

#endif /* X2_PLATFORM_SOCKET_H */
