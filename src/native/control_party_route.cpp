#include "control_party_route.hpp"

#include "../input/revive_prompt.hpp"
#include "control.h"
#include "control_command_bridge.h"
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
/* The conversation singleton and its flags (src/native/conversation.cpp).
   Observed: 0x10 once one is pending, 0x13 while shown, 0x18 while it ends
   with the party already free, 0x08 after. */
constexpr std::uint32_t kConversation = 0x00717aacu;
constexpr std::uint32_t kConversationFlags = 0x21b24u;
constexpr std::uint8_t kConversationVisible = 0x02u;
constexpr std::uint8_t kConversationEnding = 0x08u;
constexpr std::uint8_t kConversationEnabled = 0x10u;

bool conversation_holds(std::uint8_t flags) {
  const bool pending = (flags & kConversationEnabled) != 0u &&
                       (flags & kConversationEnding) == 0u;
  return (flags & kConversationVisible) != 0u || pending;
}

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

/* GET /party's body, built at the guest input poll, where the guest is
   between updates and the revive offer is polled. */
void read_party(void *context) {
  std::string &body = *static_cast<std::string *>(context);
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
  body = "{\"money\":";
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
  body += ",\"notice\":" + json_text(offer.notice);
  std::uint32_t conversation = 0;
  std::uint8_t flags = 0;
  const bool held =
      memory.read_u32(base + kConversation, &conversation) &&
      conversation != 0u &&
      memory.read(conversation + kConversationFlags, &flags, 1u) &&
      conversation_holds(flags);
  body += ",\"conversation\":";
  body += held ? "true" : "false";
  body += "}\n";
}

} // namespace

void party_route(x2_socket_t fd) {
  std::string body;
  if (control_command_guest_read(read_party, &body) < 0) {
    control_reply_text(fd, 504, "Gateway Timeout",
                       "the guest did not reach an input poll within 10s, so "
                       "the party was not read\n");
    return;
  }
  control_reply_json(fd, 200, "OK", body.c_str(), body.size());
}

} // namespace x2::control
