#include "control_party_route.hpp"

#include "../input/revive_prompt.hpp"
#include "control.h"
#include "extraction_revive.hpp"
#include "guest_memory_view.hpp"
#include "json_string.h"
#include "retail_entities.hpp"
#include "x86rt_native.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace x2::control {
namespace {

constexpr std::uint32_t kImageBase = 0x00400000u;
/* The team -1 party cache: five handles, then the count. */
constexpr std::uint32_t kPartyCache = 0x007298c8u;
constexpr std::uint32_t kPartyCount = 0x14u;
constexpr std::uint32_t kMoneyObject = 0x0072a514u;
constexpr std::uint32_t kMoney = 0x2590u;
constexpr std::uint32_t kHealth = 0x27cu;
constexpr std::uint32_t kMaxHealth = 0x284u;
constexpr std::uint32_t kEnergy = 0x288u;
constexpr std::uint32_t kMaxEnergy = 0x314u;

struct Reading {
  float health = 0.0F;
  float max_health = 0.0F;
  float energy = 0.0F;
  float max_energy = 0.0F;
};

std::string json_text(const std::string &value) {
  std::vector<char> buffer(value.size() * 6u + 3u);
  json_string_format(buffer.data(), buffer.size(), value.c_str());
  return buffer.data();
}

} // namespace

void party_route(x2_socket_t fd) {
  const native::LiveGuestMemory memory;
  const native::EntityTables tables = native::live_entity_tables();
  const std::uint32_t base = x86_module_base("XMen2.exe") - kImageBase;
  std::uint32_t money_object = 0;
  std::uint32_t money = 0;
  const bool have_money = memory.read_u32(base + kMoneyObject, &money_object) &&
                          money_object &&
                          memory.read_u32(money_object + kMoney, &money);
  std::uint32_t count = 0;
  memory.read_u32(base + kPartyCache + kPartyCount, &count);
  std::string body = "{\"money\":";
  body +=
      have_money ? std::to_string(static_cast<std::int32_t>(money)) : "null";
  body += ",\"heroes\":[";
  bool first = true;
  for (std::uint32_t i = 0; i < count && i < 5u; ++i) {
    std::uint32_t handle = 0;
    if (!memory.read_u32(base + kPartyCache + 4u * i, &handle)) {
      continue;
    }
    const auto entity = native::entity_by_handle(memory, tables, handle);
    Reading reading;
    if (!entity ||
        !memory.read(entity->address + kHealth, &reading.health, 4u) ||
        !memory.read(entity->address + kMaxHealth, &reading.max_health, 4u) ||
        !memory.read(entity->address + kEnergy, &reading.energy, 4u) ||
        !memory.read(entity->address + kMaxEnergy, &reading.max_energy, 4u)) {
      continue;
    }
    char numbers[256];
    std::snprintf(numbers, sizeof numbers,
                  "\"handle\":%u,\"health\":%.1f,\"max_health\":%.1f,"
                  "\"energy\":%.1f,\"max_energy\":%.1f,"
                  "\"x\":%.1f,\"y\":%.1f",
                  handle, static_cast<double>(reading.health),
                  static_cast<double>(reading.max_health),
                  static_cast<double>(reading.energy),
                  static_cast<double>(reading.max_energy),
                  static_cast<double>(entity->at.x),
                  static_cast<double>(entity->at.y));
    body += first ? "{" : ",{";
    first = false;
    body +=
        "\"definition\":" + json_text(entity->definition) + "," + numbers + "}";
  }
  const input::RevivePromptView offer = input::revive_prompt().view();
  body += "],\"pads\":[";
  first = true;
  for (const native::Position &pad : native::extraction_pad_positions(memory)) {
    char at[96];
    std::snprintf(at, sizeof at, "%s{\"x\":%.1f,\"y\":%.1f,\"z\":%.1f}",
                  first ? "" : ",", static_cast<double>(pad.x),
                  static_cast<double>(pad.y), static_cast<double>(pad.z));
    body += at;
    first = false;
  }
  body += "],\"offered\":";
  body += offer.offered ? "true" : "false";
  body += ",\"offer\":" + json_text(offer.text);
  body += ",\"notice\":" + json_text(offer.notice) + "}\n";
  control_reply_json(fd, 200, "OK", body.c_str(), body.size());
}

} // namespace x2::control
