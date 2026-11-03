/*
 * Reports the entity model attaches retail leaves quietly empty
 * (docs/RE/pickups.md): every CPhysicalEntity::setModel (FUN_004730f0) that
 * fails, and every CInventoryEntity::setItem (FUN_00479cc0) that keeps its
 * pickup without a model. setItem ignores setModel's result, so a refused
 * model leaves a pickup that can be used but is not drawn.
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
/* setItem(owner, item, count), thiscall, ret 0xc; AL false removes it. */
constexpr std::uint32_t kSetItem = 0x00479cc0u;
constexpr std::uint32_t kEntityHandle = 0x1cu;
constexpr std::uint32_t kEntityModel = 0x1ccu;
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

/* Why setModel refused, from what it draws from as it stood before the call. */
std::string refusal(const x2::native::ModelPools &pools) {
  for (const x2::native::ModelPool &pool : pools) {
    if (pool.set_model && pool.used >= pool.capacity) {
      return std::string("the ") + pool.name + " pool is full (" +
             std::to_string(pool.used) + " of " +
             std::to_string(pool.capacity) + ")";
    }
  }
  return "the model file did not load";
}

std::string describe(const x2::native::GuestMemoryView &memory,
                     const x2::native::PickupTables &tables,
                     std::uint32_t entity) {
  std::uint32_t handle = 0;
  memory.read_u32(entity + kEntityHandle, &handle);
  const std::optional<x2::native::EntityView> view =
      x2::native::entity_by_handle(memory, tables.entities, handle);
  if (!view) {
    return "entity " + std::to_string(handle) + " (?)";
  }
  std::string text =
      "entity " + std::to_string(handle) + " (" + view->definition;
  if (const std::optional<x2::native::PickupView> pickup =
          x2::native::pickup_view(memory, tables, *view)) {
    text += ", \"" + pickup->label + "\"";
  }
  return text + ")";
}

/* The setItem call in progress, so its setModel refusal is reported once,
   with the pickup it left empty. */
class ModelAttachReport {
public:
  void set_model(CPU *cpu) {
    const x2::native::LiveGuestMemory memory;
    const x2::native::PickupTables tables = x2::native::live_pickup_tables();
    const std::uint32_t entity = cpu->reg[kX86pEcx];
    std::uint32_t path_address = 0;
    memory.read_u32(cpu->reg[kX86pEsp] + 4u, &path_address);
    const std::string path = guest_text(memory, path_address);
    const x2::native::ModelPools before =
        x2::native::model_pools(memory, tables);
    x86_guest_body(cpu, "XMen2.exe", kSetModel);
    if ((cpu->reg[kX86pEax] & 0xffu) != 0) {
      return;
    }
    const std::string why =
        path_address == 0 ? std::string("no path") : refusal(before);
    if (entity == item_entity_) {
      item_refusal_ = "setModel refused \"" + path + "\": " + why;
      return;
    }
    lucent_log_error(
        "model", "setModel 0x004730f0: %s got no model for \"%s\": %s",
        describe(memory, tables, entity).c_str(), path.c_str(), why.c_str());
  }

  void set_item(CPU *cpu) {
    const std::uint32_t entity = cpu->reg[kX86pEcx];
    const std::uint32_t outer = item_entity_;
    item_entity_ = entity;
    item_refusal_.clear();
    x86_guest_body(cpu, "XMen2.exe", kSetItem);
    item_entity_ = outer;
    const x2::native::LiveGuestMemory memory;
    std::uint32_t model = 0;
    if ((cpu->reg[kX86pEax] & 0xffu) == 0 ||
        !memory.read_u32(entity + kEntityModel, &model) || model != 0) {
      return;
    }
    const x2::native::PickupTables tables = x2::native::live_pickup_tables();
    const std::string why =
        item_refusal_.empty() ? "its item names no model" : item_refusal_;
    lucent_log_error("model", "setItem 0x00479cc0: pickup %s has no model: %s",
                     describe(memory, tables, entity).c_str(), why.c_str());
  }

private:
  std::uint32_t item_entity_ = 0;
  std::string item_refusal_;
};

ModelAttachReport g_report;

} // namespace

extern "C" void x2_override_004730f0(CPU *cpu) { g_report.set_model(cpu); }

extern "C" void x2_override_00479cc0(CPU *cpu) { g_report.set_item(cpu); }

__attribute__((constructor)) static void x2_model_attach_report_register() {
  x86_register_override("XMen2.exe", kSetModel, x2_override_004730f0);
  x86_register_override("XMen2.exe", kSetItem, x2_override_00479cc0);
}
