#include "guest_image_reader.hpp"

#include <algorithm>

namespace x2::native {
namespace {

/* handle_str<2> pool, FUN_00602200: a heap object held here. */
inline constexpr std::uint32_t kPool2PointerRva = 0x0060a81cu;
inline constexpr std::uint32_t kPool2Slots = 0x400u;
inline constexpr std::uint32_t kPool2Strings = 0x1008u;
inline constexpr std::uint32_t kPool2Capacity = 0x1c00u;
/* handle_str<0> pool, FUN_00602140: a static object. */
inline constexpr std::uint32_t kPool0Rva = 0x0060a820u;
inline constexpr std::uint32_t kPool0Slots = 0x2000u;
inline constexpr std::uint32_t kPool0Strings = 0x8008u;
inline constexpr std::uint32_t kPool0Capacity = 0x14400u;
inline constexpr std::uint32_t kHandleIndexMask = 0x00ffffffu;

} // namespace

std::string latin1_to_utf8(const std::string &bytes) {
  std::string out;
  out.reserve(bytes.size());
  for (const char c : bytes) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 0x80u) {
      out.push_back(c);
    } else {
      out.push_back(static_cast<char>(0xc0u | (byte >> 6)));
      out.push_back(static_cast<char>(0x80u | (byte & 0x3fu)));
    }
  }
  return out;
}

GuestImageReader::GuestImageReader(const GuestMemoryView &memory,
                                   std::uint32_t image_base)
    : memory_(memory), image_base_(image_base) {}

bool GuestImageReader::fail(std::uint32_t address) {
  failed_ = address;
  return false;
}

bool GuestImageReader::bytes(std::uint32_t address, void *out,
                             std::size_t count) {
  if (memory_.read(address, out, count)) {
    return true;
  }
  return fail(address);
}

bool GuestImageReader::u32(std::uint32_t address, std::uint32_t *out) {
  return bytes(address, out, sizeof *out);
}

/* Read in chunks so the end of a mapping past the terminator is never
   touched. */
bool GuestImageReader::c_string(std::uint32_t address, std::size_t capacity,
                                std::string *out) {
  out->clear();
  constexpr std::size_t kChunk = 16u;
  char chunk[kChunk];
  std::size_t scanned = 0;
  while (scanned < capacity) {
    const auto at = static_cast<std::uint32_t>(address + scanned);
    std::size_t got = std::min(kChunk, capacity - scanned);
    if (!memory_.read(at, chunk, got)) {
      got = 1u;
      if (!bytes(at, chunk, got)) {
        return false;
      }
    }
    for (std::size_t i = 0; i < got; ++i) {
      if (chunk[i] == '\0') {
        return true;
      }
      out->push_back(chunk[i]);
    }
    scanned += got;
  }
  return fail(address);
}

/* pool + 0x1008 + pool[1 + (handle & 0xffffff)], the lookup every CMenuItem
   accessor inlines; the high byte is not checked by the game either. */
bool GuestImageReader::pool2_string(std::uint32_t handle, std::string *out) {
  out->clear();
  if (handle == 0u) {
    return true;
  }
  std::uint32_t pool = 0;
  if (!u32(image(kPool2PointerRva), &pool)) {
    return false;
  }
  const std::uint32_t index = handle & kHandleIndexMask;
  std::uint32_t offset = 0;
  if (pool == 0u || index >= kPool2Slots ||
      !u32(pool + 4u + index * 4u, &offset) || offset >= kPool2Capacity) {
    return fail(pool + 4u + index * 4u);
  }
  std::string raw;
  if (!c_string(pool + kPool2Strings + offset, kPool2Capacity - offset, &raw)) {
    return false;
  }
  *out = latin1_to_utf8(raw);
  return true;
}

bool GuestImageReader::pool0_string(std::uint32_t handle, std::string *out) {
  out->clear();
  const std::uint32_t index = handle & kHandleIndexMask;
  if (handle == 0u || index >= kPool0Slots) {
    return true;
  }
  const std::uint32_t pool = image(kPool0Rva);
  std::uint32_t offset = 0;
  if (!u32(pool + 4u + index * 4u, &offset)) {
    return false;
  }
  if (offset >= kPool0Capacity) {
    return true;
  }
  return c_string(pool + kPool0Strings + offset, kPool0Capacity - offset, out);
}

} // namespace x2::native
