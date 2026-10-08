#include "extraction_revive.hpp"

#include "../input/gameplay_control.h"
#include "../input/revive_prompt.hpp"
#include "extraction_revive_policy.hpp"
#include "guest_body.h"
#include "guest_call.hpp"
#include "guest_clock.h"
#include "guest_memory.h"
#include "guest_memory_view.hpp"
#include "retail_entities.hpp"
#include "settings_store.h"
#include "x86rt_native.h"

#include <lucent/log_c.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/* The guest side of `gameplay.extraction_revive`; addresses in
 * docs/RE/extraction.md. */
namespace x2::native {
namespace {

constexpr std::uint32_t kImageBase = 0x00400000u;

/* XMen2.exe linked addresses. */
constexpr std::uint32_t kExtractionPoint = 0x004a6b50u;
constexpr std::uint32_t kExtractionPointLite = 0x004a6d80u;
constexpr std::uint32_t kGetUpEnd = 0x004220d0u; /* timer end of slot +0x21c */
constexpr std::uint32_t kPartySingleton = 0x0046dce0u;
constexpr std::uint32_t kResolveActor = 0x0041fb10u; /* cdecl(handle) */
constexpr std::uint32_t kActorStats = 0x0041d5a0u;   /* this = actor */
constexpr std::uint32_t kStatsLevel = 0x004b87c0u; /* this = record, -> short */
constexpr std::uint32_t kMoneySingleton = 0x00480a00u;
constexpr std::uint32_t kReviveSource = 0x0069d058u; /* DAT_0069d058 */

/* Slots and fields. */
constexpr std::uint32_t kPartyListSlot = 0x120u;
constexpr std::uint32_t kMoneyGetSlot = 0x24u;
constexpr std::uint32_t kMoneySetSlot = 0x6cu;
constexpr std::uint32_t kStatsReviveSlot = 0x1cu;
constexpr std::uint32_t kStatsDeadSlot = 0x28u;
constexpr std::uint32_t kActorSetHealthSlot = 0x1a4u;
constexpr std::uint32_t kActorSetEnergySlot = 0x1a8u;
constexpr std::uint32_t kActorReviveSlot = 0x21cu;
constexpr std::uint32_t kStatsRecord = 0xc0u;
constexpr std::uint32_t kActorGetUpPending =
    0x60cu; /* zeroed once it stood up */
constexpr std::uint32_t kActorMaxHealth = 0x284u;
constexpr std::uint32_t kActorMaxEnergy = 0x314u;
constexpr std::uint32_t kEntityPosition = 0x20u;

constexpr std::uint32_t kPartyListBytes = 0x40u;
constexpr std::uint32_t kPartyHandles = 5u;
constexpr std::uint32_t kHalfHealth = 0x3f000000u; /* retail's revive 0.5f */
constexpr std::uint32_t kFullHealth = 0x3f800000u; /* 1.0f */

/* The world map pad and the saving-only sidemission pad. */
constexpr const char *kPadDefPrefix = "xtraction_point";

/* The retail nearby-revive radius (0x00428ce0). */
constexpr float kNearRadius = 480.0F;
constexpr double kScanSeconds = 0.25;

std::uint32_t exe(std::uint32_t linked) {
  return x86_module_base("XMen2.exe") + linked - kImageBase;
}

struct Vec2 {
  float x;
  float y;
};

struct Hero {
  std::uint32_t actor = 0;
  std::uint32_t record = 0; /* the hero's stats record, actor +0x35c */
  std::uint32_t stats = 0;  /* record +0xc0 */
  bool fallen = false;
  int level = 0;
  Vec2 at{};
};

struct Money {
  std::uint32_t object = 0;
  int amount = 0;
};

bool read_position(const GuestMemoryView &memory, std::uint32_t entity,
                   Vec2 *out) {
  return memory.read(entity + kEntityPosition, out, sizeof *out);
}

std::vector<Hero> read_party(const CPU &cpu) {
  std::vector<Hero> party;
  const LiveGuestMemory memory;
  guest::GuestBlock list(kPartyListBytes);
  if (!list) {
    return party;
  }
  std::memset(list.bytes(), 0, kPartyListBytes);
  guest::GuestCall call(cpu);
  const std::uint32_t singleton = call.cdecl_call(exe(kPartySingleton));
  call.virtual_call(singleton, kPartyListSlot,
                    {list.address(), 0xffffffffu, 0u});
  const std::uint32_t count = RD32(list.address() + 4u * kPartyHandles);
  for (std::uint32_t i = 0; i < count && i < kPartyHandles; ++i) {
    Hero hero;
    hero.actor =
        call.cdecl_call(exe(kResolveActor), {RD32(list.address() + 4u * i)});
    if (!hero.actor) {
      continue;
    }
    hero.record = call.thiscall(exe(kActorStats), hero.actor);
    if (!hero.record ||
        !memory.read_u32(hero.record + kStatsRecord, &hero.stats) ||
        !hero.stats || !read_position(memory, hero.actor, &hero.at)) {
      continue;
    }
    hero.fallen = (call.virtual_call(hero.stats, kStatsDeadSlot) & 0xffu) != 0u;
    hero.level = static_cast<std::int16_t>(
        call.thiscall(exe(kStatsLevel), hero.record) & 0xffffu);
    party.push_back(hero);
  }
  return party;
}

Money read_money(const CPU &cpu) {
  guest::GuestCall call(cpu);
  Money money;
  money.object = call.cdecl_call(exe(kMoneySingleton));
  money.amount =
      static_cast<std::int32_t>(call.virtual_call(money.object, kMoneyGetSlot));
  return money;
}

bool near_a_pad(const std::vector<Hero> &party,
                const std::vector<Position> &pads) {
  for (const Hero &hero : party) {
    for (const Position &pad : pads) {
      if (std::hypot(hero.at.x - pad.x, hero.at.y - pad.y) <= kNearRadius) {
        return true;
      }
    }
  }
  return false;
}

std::vector<int> fallen_levels(const std::vector<Hero> &party) {
  std::vector<int> levels;
  for (const Hero &hero : party) {
    if (hero.fallen) {
      levels.push_back(hero.level);
    }
  }
  return levels;
}

/* The retail roster revive (0x005e4010), settling at `health_fraction`. */
void revive(const CPU &cpu, const Hero &hero, std::uint32_t health_fraction) {
  guest::GuestCall call(cpu);
  call.virtual_call(hero.stats, kStatsReviveSlot);
  call.virtual_call(hero.actor, kActorSetHealthSlot,
                    {RD32(hero.actor + kActorMaxHealth)});
  call.virtual_call(hero.actor, kActorReviveSlot,
                    {0u, RD32(exe(kReviveSource)), health_fraction, 1u, 0u});
}

void refill(const CPU &cpu, const Hero &hero) {
  guest::GuestCall call(cpu);
  call.virtual_call(hero.actor, kActorSetHealthSlot,
                    {RD32(hero.actor + kActorMaxHealth)});
  call.virtual_call(hero.actor, kActorSetEnergySlot,
                    {RD32(hero.actor + kActorMaxEnergy)});
}

/* `free`: everyone back up and full; returns the actors revived. */
std::vector<std::uint32_t> restore_party(const CPU &cpu) {
  const std::vector<Hero> party = read_party(cpu);
  std::vector<std::uint32_t> revived;
  for (const Hero &hero : party) {
    if (hero.fallen) {
      revive(cpu, hero, kFullHealth);
      revived.push_back(hero.actor);
    }
    refill(cpu, hero);
  }
  lucent_log_info("extraction",
                  "free: restored %zu heroes, %zu of them revived",
                  party.size(), revived.size());
  return revived;
}

/* `paid`: take the summed cost once, then revive each fallen hero. */
void pay_and_revive(const CPU &cpu, const std::vector<Hero> &party) {
  input::RevivePrompt &prompt = input::revive_prompt();
  const Money money = read_money(cpu);
  const extraction::RevivePlan plan =
      extraction::plan_revive(fallen_levels(party), money.amount);
  if (plan.outcome == extraction::RevivePlan::Outcome::NothingFallen) {
    return;
  }
  if (plan.outcome == extraction::RevivePlan::Outcome::Insufficient) {
    prompt.announce("Not enough money: reviving costs " +
                    std::to_string(plan.total) + ", you have " +
                    std::to_string(plan.money));
    lucent_log_info("extraction", "paid: refused, cost %d money %d", plan.total,
                    plan.money);
    return;
  }
  guest::GuestCall call(cpu);
  call.virtual_call(money.object, kMoneySetSlot,
                    {static_cast<std::uint32_t>(money.amount - plan.total)});
  for (const Hero &hero : party) {
    if (hero.fallen) {
      revive(cpu, hero, kHalfHealth);
    }
  }
  prompt.announce("Revived " + std::to_string(plan.fallen) + " for " +
                  std::to_string(plan.total));
  lucent_log_info("extraction", "paid: revived %d for %d, money %d -> %d",
                  plan.fallen, plan.total, money.amount,
                  money.amount - plan.total);
}

} // namespace

std::vector<Position> extraction_pad_positions(const GuestMemoryView &memory) {
  std::vector<Position> pads;
  for (const EntityView &entity : all_entities(memory, live_entity_tables())) {
    if (entity.definition.rfind(kPadDefPrefix, 0) == 0) {
      pads.push_back(entity.at);
    }
  }
  return pads;
}

void ExtractionRevive::poll(CPU *cpu, double now) {
  input::RevivePrompt &prompt = input::revive_prompt();
  const x2::config::ExtractionRevive mode =
      x2::config::settings_store()->extraction_revive;
  const bool active =
      cpu && x2_gameplay_control_state(guest_clock_now_s()) == kX2ControlActive;
  if (mode != x2::config::ExtractionRevive::Paid || !active) {
    prompt.withdraw();
  }
  if (mode == x2::config::ExtractionRevive::Off) {
    inside_ = false;
    forget_getting_up();
  }
  if (!active || mode == x2::config::ExtractionRevive::Off) {
    return;
  }
  const bool requested =
      mode == x2::config::ExtractionRevive::Paid && prompt.take_request();
  if (!requested && now < next_scan_) {
    return;
  }
  next_scan_ = now + kScanSeconds;
  const std::vector<Hero> party = read_party(*cpu);
  const bool near =
      near_a_pad(party, extraction_pad_positions(LiveGuestMemory()));
  if (mode == x2::config::ExtractionRevive::Free) {
    if (near && !inside_) {
      for (const std::uint32_t actor : restore_party(*cpu)) {
        set_getting_up(actor, true);
      }
    }
    if (!near) {
      forget_getting_up();
    }
    inside_ = near;
    return;
  }
  const std::vector<int> levels = fallen_levels(party);
  if (levels.empty() || !near) {
    prompt.withdraw();
    return;
  }
  if (requested) {
    pay_and_revive(*cpu, party);
    return;
  }
  const extraction::RevivePlan plan =
      extraction::plan_revive(levels, read_money(*cpu).amount);
  prompt.offer(extraction::offer_text(plan),
               plan.outcome == extraction::RevivePlan::Outcome::Affordable);
}

/* 0x004220d0 re-queues itself with a fixed 0.5 when its 0x005252e0 +0x1c8
   readiness check fails, so the fraction passed to the revive is lost; a
   hero we revived gets 1.0 on every fire until the stand-up completes. */
void ExtractionRevive::get_up_end(CPU *cpu) {
  const std::uint32_t actor = cpu->reg[kX86pEcx];
  const bool revived_by_us = is_getting_up(actor);
  if (revived_by_us) {
    WR32(cpu->reg[kX86pEsp] + 8u, kFullHealth);
  }
  x86_guest_body(cpu, "XMen2.exe", kGetUpEnd);
  if (revived_by_us && RD32(actor + kActorGetUpPending) == 0u) {
    set_getting_up(actor, false);
  }
}

bool ExtractionRevive::is_getting_up(std::uint32_t actor) {
  const std::lock_guard lock(mutex_);
  return std::find(getting_up_.begin(), getting_up_.end(), actor) !=
         getting_up_.end();
}

void ExtractionRevive::set_getting_up(std::uint32_t actor, bool getting_up) {
  const std::lock_guard lock(mutex_);
  const auto at = std::find(getting_up_.begin(), getting_up_.end(), actor);
  if (getting_up && at == getting_up_.end()) {
    getting_up_.push_back(actor);
  } else if (!getting_up && at != getting_up_.end()) {
    getting_up_.erase(at);
  }
}

void ExtractionRevive::forget_getting_up() {
  const std::lock_guard lock(mutex_);
  getting_up_.clear();
}

namespace {
ExtractionRevive g_revive;

void get_up_end_override(CPU *cpu) { g_revive.get_up_end(cpu); }

__attribute__((constructor)) void register_get_up_end() {
  x86_register_override("XMen2.exe", kGetUpEnd, get_up_end_override);
}
} // namespace

void extraction_revive_poll(CPU *cpu, double now) { g_revive.poll(cpu, now); }

} // namespace x2::native
