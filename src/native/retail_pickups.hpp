#ifndef X2_RETAIL_PICKUPS_HPP
#define X2_RETAIL_PICKUPS_HPP

/* A dropped item (CInventoryEntity) and the model pools its model comes from,
 * through checked reads; layout in external/xmen2-decomp/docs/notes/pickups.md. */

#include "guest_memory_view.hpp"
#include "retail_entities.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace x2::native {

/* What setModel draws from: the model manager's instance and component
   pools, the model file cache and the resource name table. A model is
   refused while any it needs is full; the last two only refuse a file not
   cached yet. */
struct ModelPool {
  const char *name = "";
  std::uint32_t used = 0;
  std::uint32_t capacity = 0;
  /* Gates CPhysicalEntity::setModel, so a full one refuses entity models. */
  bool set_model = false;
};

inline constexpr std::size_t kModelPoolCount = 11u;
using ModelPools = std::array<ModelPool, kModelPoolCount>;

struct PickupView {
  EntityView entity;
  std::uint32_t flags = 0;
  std::string label;
  std::uint8_t quality = 0;
  std::uint16_t count = 0;
  /* The model instance (CPhysicalEntity +0x1cc); zero when none is attached. */
  std::uint32_t model = 0;
  /* Its component slots: type 0, type 2 and the scene model (type 3). */
  std::uint32_t transform = 0;
  std::uint32_t bounds = 0;
  std::uint32_t scene = 0;
  std::uint32_t scene_vtable = 0;
  /* The cached model the scene component draws; zero means not loaded. */
  std::uint32_t scene_model = 0;
  std::uint32_t scene_root = 0;
  std::uint32_t loop_fx = 0;
  bool loop_fx_on = false;
  std::uint8_t fx_level = 0;

  /* An instance is attached and its scene component holds a loaded model
     under a root node: what setModel leaves on success. */
  bool model_loaded() const {
    return model != 0 && scene != 0 && scene_model != 0 && scene_root != 0;
  }
};

/* Where the tables are in the running image: `slide` is the module's load
   address minus its linked base. */
struct PickupTables {
  EntityTables entities;
  std::uint32_t slide = 0;
};

PickupTables live_pickup_tables();

ModelPools model_pools(const GuestMemoryView &memory,
                       const PickupTables &tables);

/* The entity as a pickup; nothing when it is not a CInventoryEntity. */
std::optional<PickupView> pickup_view(const GuestMemoryView &memory,
                                      const PickupTables &tables,
                                      const EntityView &entity);

std::vector<PickupView> all_pickups(const GuestMemoryView &memory,
                                    const PickupTables &tables);

} // namespace x2::native

#endif
