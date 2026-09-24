/*
 * WS2_32's names and byte order: the resolver, the host name, dotted-quad
 * conversion, and hton/ntoh.
 *
 * gethostbyname and inet_ntoa return pointers the CALLER reads, so their
 * answers are built in guest memory -- one block for the process, reused per
 * call, which is the lifetime Winsock documents ("valid until the next call
 * on the same thread"; the game makes these calls from one thread).
 */
#include "guest_heap.h"
#include "guest_memory.h"
#include "stdcall_import.h"
#include "threads.h"
#include "winsock_posix.h"
#include "winsock_resolve.h"
#include "x86rt_native.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* The answer block: a 32-bit hostent, its name, a null alias list, up to
   MAX_ADDRESSES address pointers plus the terminator, the addresses, and the
   inet_ntoa text. */
enum {
  MAX_ADDRESSES = WINSOCK_HOST_ADDRESSES,
  NAME_BYTES = WINSOCK_HOST_NAME_BYTES,
  HOSTENT = 0,
  HOST_NAME = HOSTENT + 16,
  ALIASES = HOST_NAME + NAME_BYTES,
  ADDRESS_LIST = ALIASES + 4,
  ADDRESSES = ADDRESS_LIST + (MAX_ADDRESSES + 1) * 4,
  NTOA_TEXT = ADDRESSES + MAX_ADDRESSES * 4,
  ANSWER_BYTES = NTOA_TEXT + 16
};

static uint32_t g_answer;

static uint32_t answer_block(void) {
  if (!g_answer) {
    g_answer = guest_malloc(ANSWER_BYTES);
  }
  return g_answer;
}

static uint32_t swap32(uint32_t v) {
  return v >> 24 | (v >> 8 & 0xff00u) | (v << 8 & 0xff0000u) | v << 24;
}

/* htonl / ntohl, htons / ntohs: the guest is little-endian and the network is
   not, whatever the host is. */
void imp_WS2_32__8(CPU *C) { ret_std(C, swap32(A(0)), 1); }

void imp_WS2_32__14(CPU *C) { ret_std(C, swap32(A(0)), 1); }

void imp_WS2_32__9(CPU *C) {
  const uint32_t v = A(0) & 0xffffu;
  ret_std(C, (v >> 8 | v << 8) & 0xffffu, 1);
}

void imp_WS2_32__15(CPU *C) {
  const uint32_t v = A(0) & 0xffffu;
  ret_std(C, (v >> 8 | v << 8) & 0xffffu, 1);
}

static int guest_text(uint32_t address, char *out, size_t size) {
  size_t i = 0;
  for (; i + 1 < size; ++i) {
    if (!guest_memory_try_read(address + (uint32_t)i, &out[i], 1)) {
      return 0;
    }
    if (!out[i]) {
      return 1;
    }
  }
  out[i] = 0;
  return 1;
}

/* unsigned long inet_addr(const char *cp): INADDR_NONE for a malformed one.
   The bytes stay in network order, as the guest stores them. */
void imp_WS2_32__11(CPU *C) {
  char text[64];
  struct in_addr parsed;
  uint32_t out = 0xffffffffu;
  if (A(0) && guest_text(A(0), text, sizeof text) && inet_aton(text, &parsed)) {
    memcpy(&out, &parsed, 4);
  }
  ret_std(C, out, 1);
}

/* char *inet_ntoa(struct in_addr in) -- the address arrives by value. */
void imp_WS2_32__12(CPU *C) {
  const uint32_t block = answer_block();
  const uint32_t address = A(0);
  const uint8_t *b = (const uint8_t *)&address;
  char text[16];
  snprintf(text, sizeof text, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  guest_memory_write(block + NTOA_TEXT, text, strlen(text) + 1);
  ret_std(C, block + NTOA_TEXT, 1);
}

/* int gethostname(char *name, int namelen) */
void imp_WS2_32__57(CPU *C) {
  char name[NAME_BYTES];
  const uint32_t out = A(0), size = A(1);
  if (gethostname(name, sizeof name) != 0) {
    winsock_set_last_error(WSAENETDOWN);
    ret_std(C, WINSOCK_SOCKET_ERROR, 2);
    return;
  }
  name[sizeof name - 1] = 0;
  if (strlen(name) + 1 > size || !guest_memory_span(out, size)) {
    winsock_set_last_error(WSAEFAULT);
    ret_std(C, WINSOCK_SOCKET_ERROR, 2);
    return;
  }
  guest_memory_write(out, name, strlen(name) + 1);
  ret_std(C, 0, 2);
}

/* struct hostent *gethostbyname(const char *name) -- IPv4 answers only,
   with Windows' answers for the machine's own names (winsock_resolve.h). */
void imp_WS2_32__52(CPU *C) {
  char name[NAME_BYTES];
  WinsockHost host;
  uint32_t error = 0;
  if (!A(0) || !guest_text(A(0), name, sizeof name)) {
    winsock_set_last_error(WSAEFAULT);
    ret_std(C, 0, 1);
    return;
  }
  guest_blocking_begin();
  const int found = winsock_resolve(name, &host, &error);
  guest_blocking_end();
  if (!found) {
    winsock_set_last_error(error);
    ret_std(C, 0, 1);
    return;
  }
  const uint32_t block = answer_block();
  for (uint32_t i = 0; i < host.count; ++i) {
    guest_memory_write(block + ADDRESSES + i * 4u, &host.addresses[i], 4);
    WR32(block + ADDRESS_LIST + i * 4u, block + ADDRESSES + i * 4u);
  }
  WR32(block + ADDRESS_LIST + host.count * 4u, 0);
  WR32(block + ALIASES, 0);
  guest_memory_write(block + HOST_NAME, host.name, strlen(host.name) + 1);
  WR32(block + HOSTENT + 0u, block + HOST_NAME);
  WR32(block + HOSTENT + 4u, block + ALIASES);
  WR16(block + HOSTENT + 8u, 2);  /* h_addrtype AF_INET */
  WR16(block + HOSTENT + 10u, 4); /* h_length */
  WR32(block + HOSTENT + 12u, block + ADDRESS_LIST);
  ret_std(C, block + HOSTENT, 1);
}
