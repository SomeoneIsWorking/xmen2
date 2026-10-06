#ifndef X2_PLATFORM_SOCKET_H
#define X2_PLATFORM_SOCKET_H

/* One socket contract for the live control channel.  Routes receive an
 * opaque descriptor and never depend on POSIX fd width or Winsock lifetime. */
#if defined(_WIN32)
#include <errno.h>
#include <stddef.h>
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET x2_socket_t;
typedef int x2_socket_ssize_t;
#define X2_SOCKET_INVALID INVALID_SOCKET

static inline int x2_socket_startup(void) {
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

static inline int x2_socket_is_invalid(x2_socket_t socket) {
  return socket == X2_SOCKET_INVALID;
}

static inline int x2_socket_error_is_interrupt(void) {
  return WSAGetLastError() == WSAEINTR;
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

static inline int x2_socket_reuse_address(x2_socket_t socket) {
  const char enabled = 1;
  return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled,
                    (int)sizeof enabled);
}

static inline x2_socket_ssize_t
x2_socket_send(x2_socket_t socket, const void *buffer, size_t size) {
  return send(socket, (const char *)buffer, (int)size, 0);
}

static inline x2_socket_ssize_t x2_socket_recv(x2_socket_t socket, void *buffer,
                                               size_t size) {
  return recv(socket, (char *)buffer, (int)size, 0);
}

static inline int x2_socket_close(x2_socket_t socket) {
  return closesocket(socket);
}
#else
#include "platform_posix.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stddef.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int x2_socket_t;
typedef ssize_t x2_socket_ssize_t;
#define X2_SOCKET_INVALID (-1)

static inline int x2_socket_startup(void) { return 1; }

static inline int x2_socket_is_invalid(x2_socket_t socket) {
  return socket < 0;
}

static inline int x2_socket_error_is_interrupt(void) { return errno == EINTR; }

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

static inline int x2_socket_reuse_address(x2_socket_t socket) {
  const int enabled = 1;
  return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled);
}

static inline x2_socket_ssize_t
x2_socket_send(x2_socket_t socket, const void *buffer, size_t size) {
  return send(socket, buffer, size, 0);
}

static inline x2_socket_ssize_t x2_socket_recv(x2_socket_t socket, void *buffer,
                                               size_t size) {
  return recv(socket, buffer, size, 0);
}

static inline int x2_socket_close(x2_socket_t socket) { return close(socket); }
#endif

#endif /* X2_PLATFORM_SOCKET_H */
