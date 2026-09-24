/*
 * The Winsock translation, as it ships, against real host sockets.
 *
 * Every positive is paired with the answer that must differ: a datagram that
 * arrives and a select that reports nothing before it does, a handle socket()
 * made and one it did not, an option that translates and one that must be
 * refused rather than passed through under a wrong number.
 */
#include "../src/native/winsock_posix.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int g_checks;
static int g_failed;

#define CHECK(cond)                                                            \
  do {                                                                         \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
      g_failed++;                                                              \
      printf("  FAIL line %d: %s\n", __LINE__, #cond);                         \
    }                                                                          \
  } while (0)

static void errors(void) {
  CHECK(winsock_error_from_errno(EWOULDBLOCK) == WSAEWOULDBLOCK);
  /* A non-blocking connect under way is WOULDBLOCK on Windows. */
  CHECK(winsock_error_from_errno(EINPROGRESS) == WSAEWOULDBLOCK);
  CHECK(winsock_error_from_errno(ECONNREFUSED) == WSAECONNREFUSED);
  CHECK(winsock_error_from_errno(EADDRINUSE) == WSAEADDRINUSE);
  CHECK(winsock_error_from_errno(ENOENT) == WSAEINVAL);
  winsock_set_last_error(WSAEMSGSIZE);
  CHECK(winsock_last_error() == WSAEMSGSIZE);
}

static void addresses(void) {
  const uint8_t guest[16] = {2, 0, 0x1a, 0x0a, 127, 0, 0, 1};
  uint8_t back[16];
  struct sockaddr_in host;
  uint32_t error = 0;
  CHECK(winsock_sockaddr_to_host(guest, 16, &host, &error));
  CHECK(host.sin_family == AF_INET && ntohs(host.sin_port) == 6666 &&
        ntohl(host.sin_addr.s_addr) == 0x7f000001u);
  winsock_sockaddr_from_host(&host, back);
  CHECK(memcmp(back, guest, sizeof back) == 0);
  const uint8_t ipv6[16] = {23, 0};
  CHECK(!winsock_sockaddr_to_host(ipv6, 16, &host, &error) &&
        error == WSAEAFNOSUPPORT);
  CHECK(!winsock_sockaddr_to_host(guest, 8, &host, &error) &&
        error == WSAEFAULT);
}

static void options(void) {
  int level = 0, name = 0;
  CHECK(winsock_sockopt_to_host(0xffff, 0x20, &level, &name) &&
        level == SOL_SOCKET && name == SO_BROADCAST);
  CHECK(winsock_sockopt_to_host(0xffff, 0x1002, &level, &name) &&
        name == SO_RCVBUF);
  /* SO_LINGER carries a struct whose layout differs; it is not an int. */
  CHECK(!winsock_sockopt_to_host(0xffff, 0x80, &level, &name));
}

static void datagrams(void) {
  uint32_t error = 0;
  const int receiver = winsock_socket_open(2, 2, 0, &error);
  const int sender = winsock_socket_open(2, 2, 0, &error);
  CHECK(receiver >= 0 && sender >= 0);
  if (receiver < 0 || sender < 0) {
    return;
  }
  CHECK(winsock_is_socket((uint32_t)receiver));
  CHECK(winsock_blocking((uint32_t)receiver));
  CHECK(winsock_set_blocking((uint32_t)receiver, 0, &error));
  CHECK(!winsock_blocking((uint32_t)receiver));

  struct sockaddr_in at;
  socklen_t size = sizeof at;
  memset(&at, 0, sizeof at);
  at.sin_family = AF_INET;
  at.sin_addr.s_addr = htonl(0x7f000001u);
  CHECK(bind(receiver, (struct sockaddr *)&at, sizeof at) == 0);
  CHECK(getsockname(receiver, (struct sockaddr *)&at, &size) == 0);

  WinsockFdSet read = {1, {(uint32_t)receiver}};
  CHECK(winsock_select(&read, NULL, NULL, 0, &error) == 0 && read.count == 0);

  char buffer[8] = {0};
  CHECK(recv(receiver, buffer, sizeof buffer, 0) < 0 &&
        winsock_error_from_errno(errno) == WSAEWOULDBLOCK);

  CHECK(sendto(sender, "ping", 4, 0, (struct sockaddr *)&at, sizeof at) == 4);
  read = (WinsockFdSet){2, {(uint32_t)sender, (uint32_t)receiver}};
  CHECK(winsock_select(&read, NULL, NULL, 1000000, &error) == 1 &&
        read.count == 1 && read.handles[0] == (uint32_t)receiver);
  CHECK(recv(receiver, buffer, sizeof buffer, 0) == 4 &&
        memcmp(buffer, "ping", 4) == 0);

  const unsigned open = winsock_open_count();
  CHECK(winsock_close((uint32_t)sender, &error));
  CHECK(winsock_open_count() == open - 1);
  CHECK(!winsock_is_socket((uint32_t)sender));
  CHECK(!winsock_close((uint32_t)sender, &error) && error == WSAENOTSOCK);
  read = (WinsockFdSet){1, {(uint32_t)sender}};
  CHECK(winsock_select(&read, NULL, NULL, 0, &error) < 0 &&
        error == WSAENOTSOCK);
  CHECK(winsock_close((uint32_t)receiver, &error));
}

int main(void) {
  errors();
  addresses();
  options();
  datagrams();
  CHECK(!winsock_is_socket(0) && !winsock_is_socket(0xffffffffu));
  printf("winsock posix: %d check(s), %d failure(s)\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
