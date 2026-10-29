#include "retail_pickups.hpp"

namespace x2::native {
namespace {

/* CInventoryEntity's vtable. */
constexpr std::uint32_t kInventoryEntityVtable = 0x00687d0cu;
constexpr std::uint32_t kFlags = 0x04u;
constexpr std::uint32_t kLoopFx = 0xb4u;
constexpr std::uint32_t kLoopFxFlags = 0xc5u;
constexpr std::uint8_t kLoopFxOn = 0x04u;
constexpr std::uint32_t kFxLevel = 0xccu;
/* The "~<colour><name>" prompt label FUN_00479c70 stores. */
constexpr std::uint32_t kLabel = 0x170u;
constexpr std::uint32_t kModel = 0x1ccu;
constexpr std::uint32_t kQuality = 0x320u;
constexpr std::uint32_t kCount = 0x322u;
/* A model instance's component slots (FUN_00575ac0); the pickup descriptor
   (0x0069c4d0) puts types 0, 2 and 3 in slots 0..2. */
constexpr std::uint32_t kModelSlots = 0x08u;

/* The model manager (FUN_00575620) and the used count of each pool that
   FUN_005756e0 checks before FUN_00575c90 hands out an instance. */
constexpr std::uint32_t kModelManager = 0x007bf6d0u;
constexpr std::uint32_t kInstancePoolUsed = 0x007d200cu + 0x8280u;

struct PoolLayout {
  const char *name;
  std::uint32_t used;
  std::uint32_t capacity;
  bool set_model;
};

constexpr std::array<PoolLayout, kModelPoolCount> kPools = {{
    {"instances", kInstancePoolUsed, 0x2b4u, true},
    {"component0", kModelManager + 0x2c00u, 0x2b4u, true},
    {"component1", kModelManager + 0x67f4u, 0x104u, false},
    {"component2", kModelManager + 0x80d0u, 0x1b0u, true},
    {"component3", kModelManager + 0xe5d4u, 400u, true},
    {"component4", kModelManager + 0x103b8u, 0x20u, false},
    {"component5", kModelManager + 0x115bcu, 0xdcu, false},
    {"component6", kModelManager + 0x12858u, 0x28u, false},
    {"component7", kModelManager + 0x12928u, 1u, false},
}};

} // namespace

ModelPools model_pools(const GuestMemoryView &memory,
                       const PickupTables &tables) {
  ModelPools pools{};
  for (std::size_t i = 0; i < kPools.size(); ++i) {
    pools[i].name = kPools[i].name;
    pools[i].capacity = kPools[i].capacity;
    pools[i].set_model = kPools[i].set_model;
    if (!memory.read_u32(kPools[i].used + tables.slide, &pools[i].used)) {
      pools[i].used = 0;
    }
  }
  return pools;
}

std::optional<PickupView> pickup_view(const GuestMemoryView &memory,
                                      const PickupTables &tables,
                                      const EntityView &entity) {
  std::uint32_t vtable = 0;
  if (!memory.read_u32(entity.address, &vtable) ||
      vtable != kInventoryEntityVtable + tables.slide) {
    return std::nullopt;
  }
  PickupView view;
  view.entity = entity;
  std::uint32_t label = 0;
  std::uint8_t loop_flags = 0;
  if (!memory.read_u32(entity.address + kFlags, &view.flags) ||
      !memory.read_u32(entity.address + kLabel, &label) ||
      !memory.read_u32(entity.address + kModel, &view.model) ||
      !memory.read(entity.address + kQuality, &view.quality,
                   sizeof view.quality) ||
      !memory.read(entity.address + kCount, &view.count, sizeof view.count) ||
      !memory.read_u32(entity.address + kLoopFx, &view.loop_fx) ||
      !memory.read(entity.address + kLoopFxFlags, &loop_flags,
                   sizeof loop_flags) ||
      !memory.read(entity.address + kFxLevel, &view.fx_level,
                   sizeof view.fx_level)) {
    return std::nullopt;
  }
  view.label = pool_text(memory, tables.entities, label);
  view.loop_fx_on = (loop_flags & kLoopFxOn) != 0;
  if (view.model != 0 &&
      memory.read_u32(view.model + kModelSlots, &view.transform) &&
      memory.read_u32(view.model + kModelSlots + 4u, &view.bounds) &&
      memory.read_u32(view.model + kModelSlots + 8u, &view.scene) &&
      view.scene != 0 && memory.read_u32(view.scene, &view.scene_vtable)) {
    view.scene_vtable -= tables.slide;
  }
  return view;
}

std::vector<PickupView> all_pickups(const GuestMemoryView &memory,
                                    const PickupTables &tables) {
  std::vector<PickupView> pickups;
  for (const EntityView &entity : all_entities(memory, tables.entities)) {
    if (std::optional<PickupView> view = pickup_view(memory, tables, entity)) {
      pickups.push_back(*view);
    }
  }
  return pickups;
}

} // namespace x2::native
