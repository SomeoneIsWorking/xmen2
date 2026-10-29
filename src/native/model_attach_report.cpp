/*
 * Reports every CPhysicalEntity::setModel (FUN_004730f0) that leaves its
 * entity with no model, the quiet failure that draws a dropped item as only
 * its prompt (docs/RE/pickups.md). Retail returns false and logs nothing in
 * two places: FUN_005756e0 refuses a model while a pool it needs is full,
 * and the scene component's load (+0x178) can fail, which releases the
 * instance.
 */
#include "guest_body.h"
#include "guest_memory_view.hpp"
#include "retail_entities.hpp"
#include "retail_pickups.hpp"
#include "x86rt.h"
#include "x86rt_native.h"

#include <lucent/log_c.h>

#include <cstdint>
#include <optional>
#include <string>

namespace {

/* setModel(path, flags), thiscall, ret 8; returns the attach in AL. */
constexpr std::uint32_t kSetModel = 0x004730f0u;
constexpr std::uint32_t kEntityHandle = 0x1cu;
constexpr std::size_t kPathBytes = 96u;

std::string guest_text(const x2::native::GuestMemoryView &memory,
                       std::uint32_t address) {
  char text[kPathBytes] = {};
  for (std::size_t i = 0; address != 0 && i + 1 < kPathBytes; ++i) {
    if (!memory.read(address + i, &text[i], 1) || text[i] == '\0') {
      break;
    }
  }
  return text;
}

/* Why setModel refused, from the pools as they stood before the call. */
std::string refusal(const x2::native::ModelPools &pools) {
  for (const x2::native::ModelPool &pool : pools) {
    if (pool.set_model && pool.used >= pool.capacity) {
      return std::string("the ") + pool.name + " pool is full (" +
             std::to_string(pool.used) + " of " +
             std::to_string(pool.capacity) + ")";
    }
  }
  return "its scene component did not load the file";
}

} // namespace

extern "C" void x2_override_004730f0(CPU *cpu) {
  const x2::native::LiveGuestMemory memory;
  const x2::native::PickupTables tables = x2::native::live_pickup_tables();
  const std::uint32_t entity = cpu->reg[kX86pEcx];
  std::uint32_t path_address = 0;
  memory.read_u32(cpu->reg[kX86pEsp] + 4u, &path_address);
  const std::string path = guest_text(memory, path_address);
  const x2::native::ModelPools before = x2::native::model_pools(memory, tables);
  x86_guest_body(cpu, "XMen2.exe", kSetModel);
  if ((cpu->reg[kX86pEax] & 0xffu) != 0 || path.empty()) {
    return;
  }
  std::uint32_t handle = 0;
  memory.read_u32(entity + kEntityHandle, &handle);
  const std::optional<x2::native::EntityView> view =
      x2::native::entity_by_handle(memory, tables.entities, handle);
  const std::string definition = view ? view->definition : std::string("?");
  lucent_log_error("model", "entity %u (%s) got no model for \"%s\": %s",
                   handle, definition.c_str(), path.c_str(),
                   refusal(before).c_str());
}

__attribute__((constructor)) static void x2_model_attach_report_register() {
  x86_register_override("XMen2.exe", kSetModel, x2_override_004730f0);
}
