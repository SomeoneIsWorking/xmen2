#ifndef X2_DIAGNOSTICS_HOST_CODE_LOCATION_HPP
#define X2_DIAGNOSTICS_HOST_CODE_LOCATION_HPP

#include <cstdint>

namespace x2::diagnostics {

/* A host code address as the loaded image that holds it and the address's
 * offset from that image's load base, which is what a symbolizer takes. */
struct HostCodeLocation {
  const char *image;
  std::uintptr_t offset;
};

/* Resolves `address`; false when no loaded image holds it. Callable from a
 * fault handler: it allocates nothing. The image name stays valid until the
 * next call. */
bool host_code_location(std::uintptr_t address, HostCodeLocation *location);

/* The command that turns an image and offset into a function and line. */
#if defined(_WIN32)
inline constexpr const char *kHostSymbolizer =
    "llvm-symbolizer --relative-address --obj";
#else
inline constexpr const char *kHostSymbolizer = "addr2line -fCe";
#endif

} // namespace x2::diagnostics

#endif /* X2_DIAGNOSTICS_HOST_CODE_LOCATION_HPP */
