#ifndef X2_GUEST_CALL_HPP
#define X2_GUEST_CALL_HPP

/*
 * Calling into XMen2.exe from host C++.
 *
 * Every native owner that drives the game repeats the same few steps: copy
 * the guest CPU, push the arguments right to left, name `this` in ECX, and
 * say how many bytes the callee pops. GuestCall is that sequence, once; the
 * caller writes the retail signature and nothing else.
 */

extern "C" {
#include "guest_heap.h"
#include "x86rt.h"
}

#include "guest_memory.h"
#include "x86rt_native.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string_view>

namespace x2::guest {

class GuestCall {
public:
  explicit GuestCall(const CPU &origin) : cpu_(origin) {}

  /* __thiscall: `self` in ECX, the callee pops its arguments. */
  uint32_t thiscall(uint32_t target, uint32_t self,
                    std::initializer_list<uint32_t> arguments = {}) {
    push(arguments);
    cpu_.reg[kX86pEcx] = self;
    x86_guest_call_args(&cpu_, target, bytes(arguments));
    return cpu_.reg[kX86pEax];
  }

  /* __cdecl: the caller pops, which here means the copy is discarded. */
  uint32_t cdecl_call(uint32_t target,
                      std::initializer_list<uint32_t> arguments = {}) {
    push(arguments);
    x86_guest_call_args(&cpu_, target, 0u);
    return cpu_.reg[kX86pEax];
  }

  /* A virtual method: slot `offset` of the vtable `self` points at. */
  uint32_t virtual_call(uint32_t self, uint32_t offset,
                        std::initializer_list<uint32_t> arguments = {}) {
    return thiscall(RD32(RD32(self) + offset), self, arguments);
  }

private:
  static uint32_t bytes(std::initializer_list<uint32_t> arguments) {
    return static_cast<uint32_t>(arguments.size() * 4u);
  }

  void push(std::initializer_list<uint32_t> arguments) {
    for (auto at = std::rbegin(arguments); at != std::rend(arguments); ++at) {
      cpu_.reg[kX86pEsp] -= 4u;
      WR32(cpu_.reg[kX86pEsp], *at);
    }
  }

  CPU cpu_;
};

/* Guest heap bytes, freed with their owner. */
class GuestBlock {
public:
  explicit GuestBlock(uint32_t size) : address_(guest_malloc(size)) {}
  ~GuestBlock() {
    if (address_) {
      guest_free(address_);
    }
  }
  GuestBlock(const GuestBlock &) = delete;
  GuestBlock &operator=(const GuestBlock &) = delete;

  explicit operator bool() const { return address_ != 0u; }
  uint32_t address() const { return address_; }
  uint8_t *bytes() const {
    return static_cast<uint8_t *>(guest_memory_pointer(address_));
  }

private:
  uint32_t address_;
};

/* A NUL-terminated string the guest can read, freed with its owner. */
class GuestText {
public:
  explicit GuestText(std::string_view text)
      : block_(static_cast<uint32_t>(text.size() + 1u)) {
    if (block_) {
      std::memcpy(block_.bytes(), text.data(), text.size());
      block_.bytes()[text.size()] = 0;
    }
  }

  explicit operator bool() const { return static_cast<bool>(block_); }
  uint32_t address() const { return block_.address(); }

private:
  GuestBlock block_;
};

/* A guest C string, bounded, as host text. */
inline std::string_view guest_string(uint32_t address, size_t limit = 256u) {
  if (!address) {
    return {};
  }
  const auto *text =
      static_cast<const char *>(guest_memory_const_pointer(address));
  return {text, strnlen(text, limit)};
}

} // namespace x2::guest

#endif
