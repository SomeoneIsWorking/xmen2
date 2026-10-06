#ifndef X2_GUEST_MEMORY_VIEW_HPP
#define X2_GUEST_MEMORY_VIEW_HPP

#include <cstddef>
#include <cstdint>

namespace x2::native {

/* A checked read of guest memory; the seam a host-thread reader is tested
   through with a fake image. */
class GuestMemoryView {
public:
  GuestMemoryView() = default;
  GuestMemoryView(const GuestMemoryView &) = default;
  GuestMemoryView &operator=(const GuestMemoryView &) = default;
  GuestMemoryView(GuestMemoryView &&) = default;
  GuestMemoryView &operator=(GuestMemoryView &&) = default;
  virtual ~GuestMemoryView() = default;

  /* False when any byte of the range is not readable; `out` is then
     unspecified. */
  virtual bool read(std::uint32_t address, void *out,
                    std::size_t bytes) const = 0;

  bool read_u32(std::uint32_t address, std::uint32_t *out) const {
    return read(address, out, sizeof *out);
  }
};

/* The running guest, through guest_memory_try_read. No guest call, no lock. */
class LiveGuestMemory final : public GuestMemoryView {
public:
  bool read(std::uint32_t address, void *out, std::size_t bytes) const override;
};

} // namespace x2::native

#endif
