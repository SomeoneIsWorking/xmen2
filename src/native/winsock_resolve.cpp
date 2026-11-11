#include "winsock_resolve.h"

#include "platform_socket.h"
#include "winsock_host.h"

#include <string.h>
#include <strings.h>

#include <lucent/network_interfaces.h>

#include <string>

#if !defined(_WIN32)
#include <netdb.h>
#endif

namespace x2::native {

namespace {

enum { LOOPBACK_NET = 0x7f000000u };

int is_loopback(uint32_t network_order) {
  return (ntohl(network_order) & 0xff000000u) == LOOPBACK_NET;
}

int listed(const uint32_t *list, unsigned count, uint32_t address) {
  for (unsigned i = 0; i < count; ++i) {
    if (list[i] == address) {
      return 1;
    }
  }
  return 0;
}

/* Appends an adapter address unless it is loopback or already listed. */
void add_adapter(uint32_t address, uint32_t *out, unsigned *count) {
  if (address != 0 && !is_loopback(address) && !listed(out, *count, address)) {
    out[(*count)++] = address;
  }
}

/* The source address the kernel would use to leave by the default route. A
   UDP connect only selects a route; nothing is sent. 192.0.2.1 is TEST-NET-1,
   documentation-only, so no real peer is implied. */
int primary_address(uint32_t *out) {
  const x2::native::Socket probe =
      x2::native::socket_open(AF_INET, SOCK_DGRAM, 0);
  if (x2::native::socket_is_invalid(probe)) {
    return 0;
  }
  struct sockaddr_in to;
  memset(&to, 0, sizeof to);
  to.sin_family = AF_INET;
  to.sin_port = htons(9);
  to.sin_addr.s_addr = htonl(0xc0000201u);
  struct sockaddr_in from;
  const int found = x2::native::socket_connect(probe, &to) == 0 &&
                    x2::native::socket_name(probe, &from) == 0 &&
                    from.sin_addr.s_addr != 0 &&
                    !is_loopback(from.sin_addr.s_addr);
  x2::native::socket_close(probe);
  if (found) {
    *out = from.sin_addr.s_addr;
  }
  return found;
}

/* Every IPv4 address of an interface that is up. */
void adapter_addresses(uint32_t *out, unsigned *count, unsigned max) {
  std::string error;
  const auto interfaces = lucent::net::ipv4_interfaces(error);
  if (!interfaces) {
    return;
  }
  for (const lucent::net::Ipv4Interface &entry : *interfaces) {
    if (*count >= max) {
      return;
    }
    if (entry.up) {
      add_adapter(htonl(entry.address), out, count);
    }
  }
}

void loopback_answer(WinsockHost *out) {
  out->addresses[0] = htonl(0x7f000001u);
  out->count = 1;
}

int resolver_answer(const char *name, WinsockHost *out) {
  struct addrinfo hints;
  struct addrinfo *found = nullptr;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  if (getaddrinfo(name, nullptr, &hints, &found) != 0 || !found) {
    return 0;
  }
  for (const struct addrinfo *at = found;
       at && out->count < WINSOCK_HOST_ADDRESSES; at = at->ai_next) {
    const uint32_t address =
        reinterpret_cast<const sockaddr_in *>(at->ai_addr)->sin_addr.s_addr;
    if (!listed(out->addresses, out->count, address)) {
      out->addresses[out->count++] = address;
    }
  }
  freeaddrinfo(found);
  return out->count != 0;
}

} // namespace

unsigned winsock_local_addresses(uint32_t *out, unsigned max) {
  unsigned count = 0;
  uint32_t primary = 0;
  if (!winsock_host_ready()) {
    return 0;
  }
  if (max && primary_address(&primary)) {
    out[count++] = primary;
  }
  adapter_addresses(out, &count, max);
  return count;
}

int winsock_host_name(char *out, size_t size) {
  if (!winsock_host_ready() || x2::native::socket_host_name(out, size) != 0) {
    return 0;
  }
  out[size - 1] = 0;
  return 1;
}

int winsock_resolve(const char *name, WinsockHost *out, uint32_t *error) {
  char machine[WINSOCK_HOST_NAME_BYTES];
  memset(out, 0, sizeof *out);
  const int have_machine = winsock_host_name(machine, sizeof machine);
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
  if (strlen(name) >= sizeof out->name || !winsock_host_ready() ||
      !resolver_answer(name, out)) {
    *error = WINSOCK_HOST_NOT_FOUND;
    return 0;
  }
  strcpy(out->name, name);
  return 1;
}

} // namespace x2::native
