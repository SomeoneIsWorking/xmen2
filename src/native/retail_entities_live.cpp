#include "retail_entities.hpp"

#include "x86rt_native.h"

namespace x2::native {
namespace {

constexpr std::uint32_t kImageBase = 0x00400000u;
constexpr std::uint32_t kEntityManager = 0x00778b70u;
constexpr std::uint32_t kStringPool = 0x00a2c440u;

} // namespace

EntityTables live_entity_tables() {
  const std::uint32_t base = x86_module_base("XMen2.exe") - kImageBase;
  return {base + kEntityManager, base + kStringPool};
}

} // namespace x2::native
