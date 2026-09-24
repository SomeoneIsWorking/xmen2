#include "winsock_resolve.h"

#include "winsock_posix.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

/* getifaddrs arrived in Android's libc at API 24; below that the primary
   address alone is what the machine reports. */
#if !defined(__ANDROID__) || __ANDROID_API__ >= 24
#include <ifaddrs.h>
#define X2_HAVE_GETIFADDRS 1
#endif

enum { LOOPBACK_NET = 0x7f000000u };

static int is_loopback(uint32_t network_order) {
  return (ntohl(network_order) & 0xff000000u) == LOOPBACK_NET;
}

static int listed(const uint32_t *list, unsigned count, uint32_t address) {
  for (unsigned i = 0; i < count; ++i) {
    if (list[i] == address) {
      return 1;
    }
  }
  return 0;
}

/* The source address the kernel would use to leave by the default route. A
   UDP connect only selects a route; nothing is sent. 192.0.2.1 is TEST-NET-1,
   documentation-only, so no real peer is implied. */
static int primary_address(uint32_t *out) {
  const int probe = socket(AF_INET, SOCK_DGRAM, 0);
  if (probe < 0) {
    return 0;
  }
  struct sockaddr_in to;
  memset(&to, 0, sizeof to);
  to.sin_family = AF_INET;
  to.sin_port = htons(9);
  to.sin_addr.s_addr = htonl(0xc0000201u);
  struct sockaddr_in from;
  socklen_t size = sizeof from;
  const int found =
      connect(probe, (const struct sockaddr *)&to, sizeof to) == 0 &&
      getsockname(probe, (struct sockaddr *)&from, &size) == 0 &&
      from.sin_addr.s_addr != 0 && !is_loopback(from.sin_addr.s_addr);
  close(probe);
  if (found) {
    *out = from.sin_addr.s_addr;
  }
  return found;
}

unsigned winsock_local_addresses(uint32_t *out, unsigned max) {
  unsigned count = 0;
  uint32_t primary = 0;
  if (max && primary_address(&primary)) {
    out[count++] = primary;
  }
#ifdef X2_HAVE_GETIFADDRS
  struct ifaddrs *interfaces = NULL;
  if (getifaddrs(&interfaces) != 0) {
    return count;
  }
  for (const struct ifaddrs *at = interfaces; at && count < max;
       at = at->ifa_next) {
    if (!at->ifa_addr || at->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    const uint32_t address =
        ((const struct sockaddr_in *)at->ifa_addr)->sin_addr.s_addr;
    if (!is_loopback(address) && !listed(out, count, address)) {
      out[count++] = address;
    }
  }
  freeifaddrs(interfaces);
#endif
  return count;
}

static int own_name(char *out, size_t size) {
  if (gethostname(out, size) != 0) {
    return 0;
  }
  out[size - 1] = 0;
  return 1;
}

static void loopback_answer(WinsockHost *out) {
  out->addresses[0] = htonl(0x7f000001u);
  out->count = 1;
}

static int resolver_answer(const char *name, WinsockHost *out) {
  struct addrinfo hints, *found = NULL;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  if (getaddrinfo(name, NULL, &hints, &found) != 0 || !found) {
    return 0;
  }
  for (const struct addrinfo *at = found;
       at && out->count < WINSOCK_HOST_ADDRESSES; at = at->ai_next) {
    const uint32_t address =
        ((const struct sockaddr_in *)at->ai_addr)->sin_addr.s_addr;
    if (!listed(out->addresses, out->count, address)) {
      out->addresses[out->count++] = address;
    }
  }
  freeaddrinfo(found);
  return out->count != 0;
}

int winsock_resolve(const char *name, WinsockHost *out, uint32_t *error) {
  char machine[WINSOCK_HOST_NAME_BYTES];
  memset(out, 0, sizeof *out);
  const int have_machine = own_name(machine, sizeof machine);
  if (strcasecmp(name, "localhost") == 0) {
    strcpy(out->name, have_machine ? machine : "localhost");
    loopback_answer(out);
    return 1;
  }
  if (name[0] == 0 || (have_machine && strcasecmp(name, machine) == 0)) {
    strcpy(out->name, have_machine ? machine : "localhost");
    out->count =
        winsock_local_addresses(out->addresses, WINSOCK_HOST_ADDRESSES);
    if (!out->count) {
      loopback_answer(out);
    }
    return 1;
  }
  if (strlen(name) >= sizeof out->name || !resolver_answer(name, out)) {
    *error = WSAHOST_NOT_FOUND;
    return 0;
  }
  strcpy(out->name, name);
  return 1;
}
