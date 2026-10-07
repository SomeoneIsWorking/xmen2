#ifndef X2_GUEST_IMAGE_READER_HPP
#define X2_GUEST_IMAGE_READER_HPP

#include "guest_memory_view.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace x2::native {

/* The game's single-byte text as UTF-8, each byte its own code point. */
std::string latin1_to_utf8(const std::string &bytes);

/*
 * Checked reads of the running XMen2.exe image through the memory seam, with
 * its handle_str pools. A failed read records the address it failed at until
 * the next reset.
 */
class GuestImageReader {
public:
  /* `image_base` is where XMen2.exe is mapped. */
  GuestImageReader(const GuestMemoryView &memory, std::uint32_t image_base);

  std::uint32_t image_base() const { return image_base_; }
  /* A static of the image, by its RVA. */
  std::uint32_t image(std::uint32_t rva) const { return image_base_ + rva; }

  bool bytes(std::uint32_t address, void *out, std::size_t count);
  bool u32(std::uint32_t address, std::uint32_t *out);
  /* A NUL-terminated string found within `capacity` bytes, as raw bytes. */
  bool c_string(std::uint32_t address, std::size_t capacity, std::string *out);
  /* handle_str<2> (FUN_00602200), as UTF-8; "" for handle 0. */
  bool pool2_string(std::uint32_t handle, std::string *out);
  /* handle_str<0> (FUN_00602140), as raw bytes; "" for a handle that names no
     string in the pool. */
  bool pool0_string(std::uint32_t handle, std::string *out);

  /* Records `address` as the failure and returns false. */
  bool fail(std::uint32_t address);
  void reset_failure() { failed_ = 0u; }
  std::uint32_t failed_address() const { return failed_; }

private:
  const GuestMemoryView &memory_;
  std::uint32_t image_base_;
  std::uint32_t failed_ = 0;
};

} // namespace x2::native

#endif
