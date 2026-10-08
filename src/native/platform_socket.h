#pragma once

/* One host socket contract for the live control channel, the guest's Winsock
 * (winsock_host.cpp) and the LAN presence socket. Callers receive an opaque
 * descriptor and the host's own error code, and never depend on POSIX fd
 * width or Winsock lifetime. Every address is IPv4. */
#if defined(_WIN32)
#include <errno.h>
#include <stddef.h>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace x2::native {

using Socket = SOCKET;
using SocketSsize = int;
using PollFd = WSAPOLLFD;
inline constexpr Socket kSocketInvalid = INVALID_SOCKET;
/* WSAPoll refuses POLLPRI; a failed socket reports POLLERR without asking. */
inline constexpr int kPollIn = POLLIN;
inline constexpr int kPollOut = POLLOUT;
inline constexpr int kPollExcept = 0;
inline constexpr int kPollHup = POLLHUP;
inline constexpr int kPollErr = POLLERR;

inline int socket_startup() {
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

inline int socket_is_invalid(Socket socket) { return socket == kSocketInvalid; }

/* The host's code for the last failure: a WSA code here, errno on POSIX. */
inline int socket_error() { return WSAGetLastError(); }

inline int socket_error_is_interrupt() { return WSAGetLastError() == WSAEINTR; }

inline int socket_error_would_block(int error) {
  return error == WSAEWOULDBLOCK;
}

inline Socket socket_open(int domain, int type, int protocol) {
  return socket(domain, type, protocol);
}

inline Socket socket_accept(Socket socket) {
  return accept(socket, NULL, NULL);
}

inline int socket_bind(Socket socket, const struct sockaddr *address,
                       int address_size) {
  return bind(socket, address, address_size);
}

inline int socket_listen(Socket socket, int backlog) {
  return listen(socket, backlog);
}

inline int socket_connect(Socket socket, const struct sockaddr_in *to) {
  return connect(socket, (const struct sockaddr *)to, (int)sizeof *to);
}

/* SD_RECEIVE/SD_SEND/SD_BOTH, which are SHUT_RD/WR/RDWR's values. */
inline int socket_shutdown(Socket socket, int how) {
  return shutdown(socket, how);
}

inline int socket_reuse_address(Socket socket) {
  const char enabled = 1;
  return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled,
                    (int)sizeof enabled);
}

inline int socket_set_int_option(Socket socket, int level, int name,
                                 int value) {
  return setsockopt(socket, level, name, (const char *)&value,
                    (int)sizeof value);
}

inline int socket_set_nonblocking(Socket socket, int nonblocking) {
  u_long mode = nonblocking ? 1u : 0u;
  return ioctlsocket(socket, FIONBIO, &mode);
}

/* Bytes waiting to be read. */
inline int socket_pending(Socket socket, unsigned *bytes) {
  u_long pending = 0;
  const int rc = ioctlsocket(socket, FIONREAD, &pending);
  *bytes = (unsigned)pending;
  return rc;
}

inline int socket_name(Socket socket, struct sockaddr_in *out) {
  int size = (int)sizeof *out;
  return getsockname(socket, (struct sockaddr *)out, &size);
}

inline SocketSsize socket_send(Socket socket, const void *buffer, size_t size) {
  return send(socket, (const char *)buffer, (int)size, 0);
}

inline SocketSsize socket_recv(Socket socket, void *buffer, size_t size) {
  return recv(socket, (char *)buffer, (int)size, 0);
}

inline SocketSsize socket_send_flags(Socket socket, const void *buffer,
                                     size_t size, int flags) {
  return send(socket, (const char *)buffer, (int)size, flags);
}

inline SocketSsize socket_recv_flags(Socket socket, void *buffer, size_t size,
                                     int flags) {
  return recv(socket, (char *)buffer, (int)size, flags);
}

inline SocketSsize socket_sendto(Socket socket, const void *buffer, size_t size,
                                 int flags, const struct sockaddr_in *to) {
  return sendto(socket, (const char *)buffer, (int)size, flags,
                (const struct sockaddr *)to, (int)sizeof *to);
}

inline SocketSsize socket_recvfrom(Socket socket, void *buffer, size_t size,
                                   int flags, struct sockaddr_in *from) {
  int length = (int)sizeof *from;
  return recvfrom(socket, (char *)buffer, (int)size, flags,
                  (struct sockaddr *)from, &length);
}

/* The size of the next datagram, copied into `buffer` and truncated to it.
 * Winsock reports a truncated datagram as WSAEMSGSIZE and cannot say how
 * long it was, so it is answered as one byte longer than the buffer. */
inline SocketSsize socket_recv_datagram(Socket socket, void *buffer,
                                        size_t size) {
  const int received = recv(socket, (char *)buffer, (int)size, 0);
  if (received < 0 && WSAGetLastError() == WSAEMSGSIZE)
    return (SocketSsize)size + 1;
  return received;
}

/* poll(2)'s contract, including an empty set waiting out the timeout, which
 * WSAPoll refuses. */
inline int socket_poll(PollFd *sockets, unsigned count, int timeout_ms) {
  if (count == 0) {
    Sleep(timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
    return 0;
  }
  return WSAPoll(sockets, count, timeout_ms);
}

inline int socket_host_name(char *out, size_t size) {
  return gethostname(out, (int)size);
}

inline int socket_close(Socket socket) { return closesocket(socket); }

} // namespace x2::native
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

namespace x2::native {

using Socket = int;
using SocketSsize = ssize_t;
using PollFd = struct pollfd;
inline constexpr Socket kSocketInvalid = -1;
inline constexpr int kPollIn = POLLIN;
inline constexpr int kPollOut = POLLOUT;
inline constexpr int kPollExcept = POLLPRI;
inline constexpr int kPollHup = POLLHUP;
inline constexpr int kPollErr = POLLERR;

/* A write to a closed peer is an error return, never SIGPIPE; Darwin has
   SO_NOSIGPIPE instead. */
#ifdef MSG_NOSIGNAL
inline constexpr int kSocketSendFlags = MSG_NOSIGNAL;
#else
inline constexpr int kSocketSendFlags = 0;
#endif

inline int socket_startup() { return 1; }

inline int socket_is_invalid(Socket socket) { return socket < 0; }

inline int socket_error() { return errno; }

inline int socket_error_is_interrupt() { return errno == EINTR; }

inline int socket_error_would_block(int error) {
  return error == EAGAIN || error == EWOULDBLOCK;
}

inline Socket socket_open(int domain, int type, int protocol) {
  return socket(domain, type, protocol);
}

inline Socket socket_accept(Socket socket) {
  return accept(socket, NULL, NULL);
}

inline int socket_bind(Socket socket, const struct sockaddr *address,
                       socklen_t address_size) {
  return bind(socket, address, address_size);
}

inline int socket_listen(Socket socket, int backlog) {
  return listen(socket, backlog);
}

inline int socket_connect(Socket socket, const struct sockaddr_in *to) {
  return connect(socket, (const struct sockaddr *)to, sizeof *to);
}

inline int socket_shutdown(Socket socket, int how) {
  return shutdown(socket, how);
}

inline int socket_reuse_address(Socket socket) {
  const int enabled = 1;
  return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled);
}

inline int socket_set_int_option(Socket socket, int level, int name,
                                 int value) {
  return setsockopt(socket, level, name, &value, sizeof value);
}

inline int socket_set_nonblocking(Socket socket, int nonblocking) {
  const int flags = fcntl(socket, F_GETFL);
  if (flags < 0)
    return -1;
  return fcntl(socket, F_SETFL,
               nonblocking ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
}

inline int socket_pending(Socket socket, unsigned *bytes) {
  int pending = 0;
  const int rc = ioctl(socket, FIONREAD, &pending);
  *bytes = (unsigned)pending;
  return rc;
}

inline int socket_name(Socket socket, struct sockaddr_in *out) {
  socklen_t size = sizeof *out;
  return getsockname(socket, (struct sockaddr *)out, &size);
}

inline SocketSsize socket_send(Socket socket, const void *buffer, size_t size) {
  return send(socket, buffer, size, 0);
}

inline SocketSsize socket_recv(Socket socket, void *buffer, size_t size) {
  return recv(socket, buffer, size, 0);
}

inline SocketSsize socket_send_flags(Socket socket, const void *buffer,
                                     size_t size, int flags) {
  return send(socket, buffer, size, flags | kSocketSendFlags);
}

inline SocketSsize socket_recv_flags(Socket socket, void *buffer, size_t size,
                                     int flags) {
  return recv(socket, buffer, size, flags);
}

inline SocketSsize socket_sendto(Socket socket, const void *buffer, size_t size,
                                 int flags, const struct sockaddr_in *to) {
  return sendto(socket, buffer, size, flags | kSocketSendFlags,
                (const struct sockaddr *)to, sizeof *to);
}

inline SocketSsize socket_recvfrom(Socket socket, void *buffer, size_t size,
                                   int flags, struct sockaddr_in *from) {
  socklen_t length = sizeof *from;
  return recvfrom(socket, buffer, size, flags, (struct sockaddr *)from,
                  &length);
}

/* The size of the next datagram, copied into `buffer` and truncated to it. */
inline SocketSsize socket_recv_datagram(Socket socket, void *buffer,
                                        size_t size) {
#ifdef MSG_TRUNC
  return recv(socket, buffer, size, MSG_TRUNC);
#else
  return recv(socket, buffer, size, 0);
#endif
}

inline int socket_poll(PollFd *sockets, unsigned count, int timeout_ms) {
  return poll(sockets, (nfds_t)count, timeout_ms);
}

inline int socket_host_name(char *out, size_t size) {
  return gethostname(out, size);
}

inline int socket_close(Socket socket) { return close(socket); }

} // namespace x2::native
#endif
