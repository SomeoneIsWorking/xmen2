#ifndef X2_RETAIL_ENTITIES_HPP
#define X2_RETAIL_ENTITIES_HPP

/* The retail entity table through checked reads; layout in
 * docs/RE/extraction.md. */

#include "guest_memory_view.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace x2::native {

/* Where the manager and the string pool are in the running image. */
struct EntityTables {
  std::uint32_t manager = 0;
  std::uint32_t pool = 0;
};

struct Position {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

struct EntityView {
  std::uint32_t address = 0;
  std::uint32_t handle = 0;
  std::string definition;
  Position at;
};

/* The tables of the running XMen2.exe. */
EntityTables live_entity_tables();

/* The text of a string-pool handle, empty when it is not readable. */
std::string pool_text(const GuestMemoryView &memory, const EntityTables &tables,
                      std::uint32_t handle);

/* One entity by handle; nothing when the slot is empty, stale or unreadable. */
std::optional<EntityView> entity_by_handle(const GuestMemoryView &memory,
                                           const EntityTables &tables,
                                           std::uint32_t handle);

/* Every entity in the table, in slot order. */
std::vector<EntityView> all_entities(const GuestMemoryView &memory,
                                     const EntityTables &tables);

} // namespace x2::native

#endif
