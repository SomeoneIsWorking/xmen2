/*
 * The Winsock translation, as it ships, against real host sockets.
 *
 * Every positive is paired with the answer that must differ: a datagram that
 * arrives and a select that reports nothing before it does, a handle socket()
 * made and one it did not, an option that translates and one that must be
 * refused rather than passed through under a wrong number.
 */
#include "../src/native/winsock_posix.h"
#include "../src/native/winsock_resolve.h"

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

static int all_loopback(const WinsockHost *host) {
  for (unsigned i = 0; i < host->count; ++i) {
    if ((ntohl(host->addresses[i]) >> 24) != 127u) {
      return 0;
    }
  }
  return 1;
}

/* The game learns its LAN address by resolving "localhost" and then the h_name
   that came back; both steps must answer as Windows does. */
static void names(void) {
  char machine[WINSOCK_HOST_NAME_BYTES];
  WinsockHost host;
  uint32_t error = 0;
  CHECK(gethostname(machine, sizeof machine) == 0);

  CHECK(winsock_resolve("localhost", &host, &error));
  CHECK(strcmp(host.name, machine) == 0);
  CHECK(host.count == 1 && host.addresses[0] == htonl(0x7f000001u));

  uint32_t local[WINSOCK_HOST_ADDRESSES];
  const unsigned adapters = winsock_local_addresses(local, 8);
  CHECK(winsock_resolve(machine, &host, &error));
  CHECK(strcmp(host.name, machine) == 0 && host.count >= 1);
  if (adapters) {
    /* An adapter address exists, so loopback must not be what comes back,
       and the primary one leads. */
    CHECK(!all_loopback(&host) && host.addresses[0] == local[0]);
  } else {
    CHECK(host.count == 1 && all_loopback(&host));
  }
  printf("  own name answers %u address(es), %u adapter(s)\n", host.count,
         adapters);

  CHECK(winsock_resolve("127.0.0.1", &host, &error) && host.count == 1 &&
        host.addresses[0] == htonl(0x7f000001u));
  error = 0;
  CHECK(!winsock_resolve("no-such-host.invalid", &host, &error) &&
        error == WSAHOST_NOT_FOUND);
}

static int broadcast_reaches(int receiver, uint16_t port) {
  const int sender = socket(AF_INET, SOCK_DGRAM, 0);
  const int on = 1;
  struct sockaddr_in to;
  char buffer[8] = {0};
  memset(&to, 0, sizeof to);
  to.sin_family = AF_INET;
  to.sin_port = port;
  to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  setsockopt(sender, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
  const int sent =
      sendto(sender, "bcast", 5, 0, (struct sockaddr *)&to, sizeof to) == 5;
  close(sender);
  WinsockFdSet read = {1, {(uint32_t)receiver}};
  uint32_t error = 0;
  return sent && winsock_select(&read, NULL, NULL, 500000, &error) == 1 &&
         recv(receiver, buffer, sizeof buffer, 0) == 5;
}

/* A socket bound to the machine's LAN address hears that LAN's broadcasts on
   Windows; the game's transport binds exactly that way and finds hosts by
   broadcast. The raw POSIX bind beside it is the instrument's negative. */
static void adapter_bind(void) {
  uint32_t local[WINSOCK_HOST_ADDRESSES], error = 0;
  if (!winsock_local_addresses(local, 1)) {
    printf("  adapter bind: NOT RUN, this host has no adapter address\n");
    return;
  }
  const int game = winsock_socket_open(2, 2, 0, &error);
  const int raw = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in at, seen;
  memset(&at, 0, sizeof at);
  at.sin_family = AF_INET;
  at.sin_addr.s_addr = local[0];
  CHECK(winsock_bind((uint32_t)game, &at, &error));
  CHECK(winsock_getsockname((uint32_t)game, &seen, &error) &&
        seen.sin_addr.s_addr == local[0] && seen.sin_port != 0);
  CHECK(broadcast_reaches(game, seen.sin_port));

  CHECK(bind(raw, (struct sockaddr *)&at, sizeof at) == 0);
  socklen_t size = sizeof at;
  CHECK(getsockname(raw, (struct sockaddr *)&at, &size) == 0);
  CHECK(!broadcast_reaches(raw, at.sin_port));
  close(raw);

  CHECK(winsock_close((uint32_t)game, &error));

  /* An address that is not this machine's binds as given, and fails. */
  const int stranger = winsock_socket_open(2, 2, 0, &error);
  at.sin_port = 0;
  at.sin_addr.s_addr = htonl(0xc0000201u);
  error = 0;
  CHECK(!winsock_bind((uint32_t)stranger, &at, &error) &&
        error == WSAEADDRNOTAVAIL);
  CHECK(winsock_close((uint32_t)stranger, &error));
}

int main(void) {
  errors();
  addresses();
  options();
  datagrams();
  names();
  adapter_bind();
  CHECK(!winsock_is_socket(0) && !winsock_is_socket(0xffffffffu));
  printf("winsock posix: %d check(s), %d failure(s)\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
