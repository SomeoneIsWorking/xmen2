/*
 * The Winsock translation, as it ships, against real host sockets.
 *
 * Every positive is paired with the answer that must differ: a datagram that
 * arrives and a select that reports nothing before it does, a handle socket()
 * made and one it did not, an option that translates and one that must be
 * refused rather than passed through under a wrong number.
 */
#include "../src/native/platform_socket.h"
#include "../src/native/winsock_host.h"
#include "../src/native/winsock_resolve.h"

#include <stdio.h>
#include <string.h>

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
#if !defined(_WIN32)
  CHECK(x2::native::winsock_error_from_errno(EWOULDBLOCK) ==
        x2::native::WINSOCK_EWOULDBLOCK);
  /* A non-blocking connect under way is WOULDBLOCK on Windows. */
  CHECK(x2::native::winsock_error_from_errno(EINPROGRESS) ==
        x2::native::WINSOCK_EWOULDBLOCK);
  CHECK(x2::native::winsock_error_from_errno(ECONNREFUSED) ==
        x2::native::WINSOCK_ECONNREFUSED);
  CHECK(x2::native::winsock_error_from_errno(EADDRINUSE) ==
        x2::native::WINSOCK_EADDRINUSE);
  CHECK(x2::native::winsock_error_from_errno(ENOENT) ==
        x2::native::WINSOCK_EINVAL);
#endif
  x2::native::winsock_set_last_error(x2::native::WINSOCK_EMSGSIZE);
  CHECK(x2::native::winsock_last_error() == x2::native::WINSOCK_EMSGSIZE);
}

static void addresses(void) {
  const uint8_t guest[16] = {2, 0, 0x1a, 0x0a, 127, 0, 0, 1};
  uint8_t back[16];
  struct sockaddr_in host;
  uint32_t error = 0;
  CHECK(x2::native::winsock_sockaddr_to_host(guest, 16, &host, &error));
  CHECK(host.sin_family == AF_INET && ntohs(host.sin_port) == 6666 &&
        ntohl(host.sin_addr.s_addr) == 0x7f000001u);
  x2::native::winsock_sockaddr_from_host(&host, back);
  CHECK(memcmp(back, guest, sizeof back) == 0);
  const uint8_t ipv6[16] = {23, 0};
  CHECK(!x2::native::winsock_sockaddr_to_host(ipv6, 16, &host, &error) &&
        error == x2::native::WINSOCK_EAFNOSUPPORT);
  CHECK(!x2::native::winsock_sockaddr_to_host(guest, 8, &host, &error) &&
        error == x2::native::WINSOCK_EFAULT);
}

/* inet_addr's forms, each against its expected network-order value. */
static void dotted(void) {
  uint32_t out = 0;
  CHECK(x2::native::winsock_parse_ipv4("192.168.1.20", &out) &&
        out == htonl(0xc0a80114u));
  CHECK(x2::native::winsock_parse_ipv4("127.1", &out) &&
        out == htonl(0x7f000001u));
  CHECK(x2::native::winsock_parse_ipv4("10.1.258", &out) &&
        out == htonl(0x0a010102u));
  CHECK(x2::native::winsock_parse_ipv4("0x7f.0.0.01", &out) &&
        out == htonl(0x7f000001u));
  CHECK(x2::native::winsock_parse_ipv4("255.255.255.255", &out) &&
        out == 0xffffffffu);
  CHECK(!x2::native::winsock_parse_ipv4("256.0.0.1", &out));
  CHECK(!x2::native::winsock_parse_ipv4("1.2.3.4.5", &out));
  CHECK(!x2::native::winsock_parse_ipv4("1.2.3.x", &out));
  CHECK(!x2::native::winsock_parse_ipv4("08.0.0.1", &out));
  CHECK(!x2::native::winsock_parse_ipv4("", &out));
  CHECK(!x2::native::winsock_parse_ipv4("1..2", &out));
}

static void options(void) {
  int level = 0, name = 0;
  CHECK(x2::native::winsock_sockopt_to_host(0xffff, 0x20, &level, &name) &&
        level == SOL_SOCKET && name == SO_BROADCAST);
  CHECK(x2::native::winsock_sockopt_to_host(0xffff, 0x1002, &level, &name) &&
        name == SO_RCVBUF);
  /* SO_LINGER carries a struct whose layout differs; it is not an int. */
  CHECK(!x2::native::winsock_sockopt_to_host(0xffff, 0x80, &level, &name));
}

static struct sockaddr_in loopback(uint16_t port_network_order) {
  struct sockaddr_in at;
  memset(&at, 0, sizeof at);
  at.sin_family = AF_INET;
  at.sin_port = port_network_order;
  at.sin_addr.s_addr = htonl(0x7f000001u);
  return at;
}

static void datagrams(void) {
  uint32_t error = 0;
  const int receiver = x2::native::winsock_socket_open(2, 2, 0, &error);
  const int sender = x2::native::winsock_socket_open(2, 2, 0, &error);
  CHECK(receiver > 0 && sender > 0);
  if (receiver <= 0 || sender <= 0) {
    return;
  }
  const uint32_t r = (uint32_t)receiver, s = (uint32_t)sender;
  CHECK(x2::native::winsock_is_socket(r));
  CHECK(x2::native::winsock_blocking(r));
  CHECK(x2::native::winsock_set_blocking(r, 0, &error));
  CHECK(!x2::native::winsock_blocking(r));

  struct sockaddr_in at = loopback(0);
  CHECK(x2::native::winsock_bind(r, &at, &error));
  CHECK(x2::native::winsock_getsockname(r, &at, &error) && at.sin_port != 0);

  x2::native::WinsockFdSet read = {1, {r}};
  CHECK(x2::native::winsock_select(&read, NULL, NULL, 0, &error) == 0 &&
        read.count == 0);

  char buffer[8] = {0};
  CHECK(x2::native::winsock_recv(r, buffer, sizeof buffer, 0, &error) < 0 &&
        error == x2::native::WINSOCK_EWOULDBLOCK);

  CHECK(x2::native::winsock_sendto(s, "ping", 4, 0, &at, &error) == 4);
  read = (x2::native::WinsockFdSet){2, {s, r}};
  CHECK(x2::native::winsock_select(&read, NULL, NULL, 1000000, &error) == 1 &&
        read.count == 1 && read.handles[0] == r);
  uint32_t pending = 0;
  CHECK(x2::native::winsock_pending(r, &pending, &error) && pending >= 4);
  struct sockaddr_in from;
  CHECK(x2::native::winsock_recvfrom(r, buffer, sizeof buffer, 0, &from,
                                     &error) == 4 &&
        memcmp(buffer, "ping", 4) == 0 &&
        from.sin_addr.s_addr == htonl(0x7f000001u));

  const unsigned open = x2::native::winsock_open_count();
  CHECK(x2::native::winsock_close(s, &error));
  CHECK(x2::native::winsock_open_count() == open - 1);
  CHECK(!x2::native::winsock_is_socket(s));
  CHECK(!x2::native::winsock_close(s, &error) &&
        error == x2::native::WINSOCK_ENOTSOCK);
  read = (x2::native::WinsockFdSet){1, {s}};
  CHECK(x2::native::winsock_select(&read, NULL, NULL, 0, &error) < 0 &&
        error == x2::native::WINSOCK_ENOTSOCK);
  CHECK(x2::native::winsock_close(r, &error));
}

static int all_loopback(const x2::native::WinsockHost *host) {
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
  char machine[x2::native::WINSOCK_HOST_NAME_BYTES];
  x2::native::WinsockHost host;
  uint32_t error = 0;
  CHECK(x2::native::winsock_host_name(machine, sizeof machine));

  CHECK(x2::native::winsock_resolve("localhost", &host, &error));
  CHECK(strcmp(host.name, machine) == 0);
  CHECK(host.count == 1 && host.addresses[0] == htonl(0x7f000001u));

  uint32_t local[x2::native::WINSOCK_HOST_ADDRESSES];
  const unsigned adapters = x2::native::winsock_local_addresses(local, 8);
  CHECK(x2::native::winsock_resolve(machine, &host, &error));
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

  CHECK(x2::native::winsock_resolve("127.0.0.1", &host, &error) &&
        host.count == 1 && host.addresses[0] == htonl(0x7f000001u));
  error = 0;
  CHECK(!x2::native::winsock_resolve("no-such-host.invalid", &host, &error) &&
        error == x2::native::WINSOCK_HOST_NOT_FOUND);
}

static int send_broadcast(uint16_t port) {
  uint32_t error = 0;
  const int sender = x2::native::winsock_socket_open(2, 2, 0, &error);
  int level = 0, name = 0;
  struct sockaddr_in to;
  memset(&to, 0, sizeof to);
  to.sin_family = AF_INET;
  to.sin_port = port;
  to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  const int sent =
      sender > 0 &&
      x2::native::winsock_sockopt_to_host(0xffff, 0x20, &level, &name) &&
      x2::native::winsock_set_option((uint32_t)sender, level, name, 1,
                                     &error) &&
      x2::native::winsock_sendto((uint32_t)sender, "bcast", 5, 0, &to,
                                 &error) == 5;
  if (sender > 0) {
    x2::native::winsock_close((uint32_t)sender, &error);
  }
  return sent;
}

static int broadcast_reaches(uint32_t receiver, uint16_t port) {
  uint32_t error = 0;
  char buffer[8] = {0};
  const int sent = send_broadcast(port);
  x2::native::WinsockFdSet read = {1, {receiver}};
  return sent &&
         x2::native::winsock_select(&read, NULL, NULL, 500000, &error) == 1 &&
         x2::native::winsock_recv(receiver, buffer, sizeof buffer, 0, &error) ==
             5;
}

/* A socket bound to the machine's LAN address hears that LAN's broadcasts on
   Windows; the game's transport binds exactly that way and finds hosts by
   broadcast. */
static void adapter_bind(void) {
  uint32_t local[x2::native::WINSOCK_HOST_ADDRESSES], error = 0;
  if (!x2::native::winsock_local_addresses(local, 1)) {
    printf("  adapter bind: NOT RUN, this host has no adapter address\n");
    return;
  }
  const int game = x2::native::winsock_socket_open(2, 2, 0, &error);
  struct sockaddr_in at, seen;
  memset(&at, 0, sizeof at);
  at.sin_family = AF_INET;
  at.sin_addr.s_addr = local[0];
  CHECK(x2::native::winsock_bind((uint32_t)game, &at, &error));
  CHECK(x2::native::winsock_getsockname((uint32_t)game, &seen, &error) &&
        seen.sin_addr.s_addr == local[0] && seen.sin_port != 0);
  CHECK(broadcast_reaches((uint32_t)game, seen.sin_port));
  CHECK(x2::native::winsock_close((uint32_t)game, &error));

#if !defined(_WIN32)
  /* The instrument's negative: a raw POSIX bind to the same address does not
     hear the broadcast, which is why winsock_bind widens it. */
  const x2::native::Socket raw =
      x2::native::socket_open(AF_INET, SOCK_DGRAM, 0);
  CHECK(x2::native::socket_bind(raw, (struct sockaddr *)&at, sizeof at) == 0);
  CHECK(x2::native::socket_name(raw, &seen) == 0);
  CHECK(send_broadcast(seen.sin_port));
  x2::native::PollFd poll = {raw, x2::native::kPollIn, 0};
  CHECK(x2::native::socket_poll(&poll, 1, 500) == 0);
  x2::native::socket_close(raw);
#endif

  /* An address that is not this machine's binds as given, and fails. */
  const int stranger = x2::native::winsock_socket_open(2, 2, 0, &error);
  at.sin_port = 0;
  at.sin_addr.s_addr = htonl(0xc0000201u);
  error = 0;
  CHECK(!x2::native::winsock_bind((uint32_t)stranger, &at, &error) &&
        error == x2::native::WINSOCK_EADDRNOTAVAIL);
  CHECK(x2::native::winsock_close((uint32_t)stranger, &error));
}

int main(void) {
  errors();
  addresses();
  dotted();
  options();
  datagrams();
  names();
  adapter_bind();
  CHECK(!x2::native::winsock_is_socket(0) &&
        !x2::native::winsock_is_socket(0xffffffffu));
  printf("winsock host: %d check(s), %d failure(s)\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
