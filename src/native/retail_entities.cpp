#include "retail_entities.hpp"

namespace x2::native {
namespace {

constexpr std::uint32_t kTable = 0x4u;
constexpr std::uint32_t kMask = 0xc3cu;
constexpr std::uint32_t kEntityDefinition = 0x14u;
constexpr std::uint32_t kEntityHandle = 0x1cu;
constexpr std::uint32_t kEntityPosition = 0x20u;
constexpr std::uint32_t kPoolIndex = 4u;
constexpr std::uint32_t kPoolText = 0x4008u;
constexpr std::uint32_t kPoolIndexMask = 0xffffffu;
constexpr std::size_t kTextBytes = 48u;
constexpr std::uint32_t kSlotLimit = 4096u;

std::optional<EntityView> read_entity(const GuestMemoryView &memory,
                                      const EntityTables &tables,
                                      std::uint32_t address) {
  EntityView view;
  view.address = address;
  std::uint32_t definition = 0;
  if (!memory.read_u32(address + kEntityHandle, &view.handle) ||
      !memory.read_u32(address + kEntityDefinition, &definition) ||
      !memory.read(address + kEntityPosition, &view.at, sizeof view.at)) {
    return std::nullopt;
  }
  view.definition = pool_text(memory, tables, definition);
  return view;
}

} // namespace

std::string pool_text(const GuestMemoryView &memory, const EntityTables &tables,
                      std::uint32_t handle) {
  std::uint32_t offset = 0;
  if (!handle ||
      !memory.read_u32(
          tables.pool + kPoolIndex + (handle & kPoolIndexMask) * 4u, &offset)) {
    return {};
  }
  char text[kTextBytes] = {};
  if (!memory.read(tables.pool + kPoolText + offset, text, sizeof text - 1)) {
    return {};
  }
  return text;
}

std::optional<EntityView> entity_by_handle(const GuestMemoryView &memory,
                                           const EntityTables &tables,
                                           std::uint32_t handle) {
  std::uint32_t mask = 0;
  std::uint32_t address = 0;
  if (!handle || !memory.read_u32(tables.manager + kMask, &mask) ||
      !memory.read_u32(tables.manager + kTable + 4u * (handle & mask),
                       &address) ||
      !address) {
    return std::nullopt;
  }
  std::optional<EntityView> view = read_entity(memory, tables, address);
  if (!view || view->handle != handle) {
    return std::nullopt;
  }
  return view;
}

std::vector<EntityView> all_entities(const GuestMemoryView &memory,
                                     const EntityTables &tables) {
  std::vector<EntityView> entities;
  std::uint32_t mask = 0;
  if (!memory.read_u32(tables.manager + kMask, &mask)) {
    return entities;
  }
  for (std::uint32_t slot = 0; slot <= mask && slot < kSlotLimit; ++slot) {
    std::uint32_t address = 0;
    if (!memory.read_u32(tables.manager + kTable + 4u * slot, &address) ||
        !address) {
      continue;
    }
    if (std::optional<EntityView> view = read_entity(memory, tables, address)) {
      entities.push_back(*view);
    }
  }
  return entities;
}

} // namespace x2::native
