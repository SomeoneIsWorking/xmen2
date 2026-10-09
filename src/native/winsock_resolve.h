#pragma once

/*
 * gethostbyname as Windows answers it, over the host's resolver.
 *
 * Two answers differ from what a POSIX resolver gives, and the game depends on
 * both to learn its own LAN address (FUN_00615d30 asks for "localhost", then
 * for the h_name that came back):
 *
 *  - "localhost" is named after the MACHINE: h_name is the host name, the
 *    address is loopback.
 *  - the machine's own name (or "") lists its adapters' IPv4 addresses, never
 *    loopback while an adapter has one. Linux resolvers commonly map the host
 *    name to 127.0.0.1 or 127.0.1.1 instead, which would make the game
 *    advertise an address no other machine can reach.
 *
 * The adapter holding the default route comes first, because callers read
 * h_addr_list[0].
 */

#include <cstddef>
#include <cstdint>

namespace x2::native {

inline constexpr int WINSOCK_HOST_NAME_BYTES = 256;
inline constexpr int WINSOCK_HOST_ADDRESSES = 8;

struct WinsockHost {
  char name[WINSOCK_HOST_NAME_BYTES];
  /* In network byte order, as a guest in_addr holds them. */
  uint32_t addresses[WINSOCK_HOST_ADDRESSES];
  unsigned count;
};

/* Returns 1 with *out filled, or 0 with *error set (WINSOCK_HOST_NOT_FOUND).
   May block on the host resolver. */
int winsock_resolve(const char *name, WinsockHost *out, uint32_t *error);

/* The machine's own name, as gethostname gives it, terminated within `size`.
   Returns 0 when the host has none. */
int winsock_host_name(char *out, size_t size);

/* The machine's own IPv4 addresses, primary first, without loopback. Returns
   the count; 0 when no adapter has an address. */
unsigned winsock_local_addresses(uint32_t *out, unsigned max);

} // namespace x2::native
