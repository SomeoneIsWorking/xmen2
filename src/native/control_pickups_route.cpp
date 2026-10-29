#include "control_pickups_route.hpp"

#include "control.h"
#include "control_command_bridge.h"
#include "control_query.h"
#include "guest_memory_view.hpp"
#include "json_string.h"
#include "retail_pickups.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace x2::control {
namespace {

struct PickupsRequest {
  std::uint32_t handle = 0;
  std::string body;
};

std::string pickup_json(const native::PickupView &pickup) {
  std::array<char, 512> numbers{};
  std::snprintf(
      numbers.data(), numbers.size(),
      "\"handle\":%u,\"address\":\"0x%08x\",\"x\":%.1f,\"y\":%.1f,"
      "\"z\":%.1f,\"flags\":\"0x%08x\",\"quality\":%u,\"count\":%u,"
      "\"model\":{\"instance\":\"0x%08x\",\"transform\":\"0x%08x\","
      "\"bounds\":\"0x%08x\",\"scene\":\"0x%08x\",\"scene_vtable\":\"0x%08x\"},"
      "\"loop_fx\":{\"id\":%u,\"on\":%s,\"level\":%u}",
      pickup.entity.handle, pickup.entity.address,
      static_cast<double>(pickup.entity.at.x),
      static_cast<double>(pickup.entity.at.y),
      static_cast<double>(pickup.entity.at.z), pickup.flags, pickup.quality,
      pickup.count, pickup.model, pickup.transform, pickup.bounds,
      pickup.scene, pickup.scene_vtable, pickup.loop_fx,
      pickup.loop_fx_on ? "true" : "false", pickup.fx_level);
  return "{\"definition\":" + native::json_string(pickup.entity.definition) +
         ",\"label\":" + native::json_string(pickup.label) + "," + numbers.data() + "}";
}

void read_pickups(void *context) {
  PickupsRequest &request = *static_cast<PickupsRequest *>(context);
  const native::LiveGuestMemory memory;
  const native::PickupTables tables = native::live_pickup_tables();
  std::string &body = request.body;
  body = "{\"pools\":[";
  bool first = true;
  for (const native::ModelPool &pool : native::model_pools(memory, tables)) {
    std::array<char, 128> line{};
    std::snprintf(line.data(), line.size(),
                  "%s{\"name\":\"%s\",\"used\":%u,\"capacity\":%u}",
                  first ? "" : ",", pool.name, pool.used, pool.capacity);
    body += line.data();
    first = false;
  }
  body += "],\"pickups\":[";
  first = true;
  for (const native::PickupView &pickup : native::all_pickups(memory, tables)) {
    if (request.handle != 0 && pickup.entity.handle != request.handle) {
      continue;
    }
    body += first ? "" : ",";
    body += pickup_json(pickup);
    first = false;
  }
  body += "]}\n";
}

} // namespace

void pickups_route(x2::native::Socket fd, const char *query) {
  PickupsRequest request;
  std::array<char, 32> handle{};
  if (control_query_arg(query, "handle", handle.data(), handle.size())) {
    request.handle =
        static_cast<std::uint32_t>(std::strtoul(handle.data(), nullptr, 10));
  }
  if (control_command_guest_read(read_pickups, &request) < 0) {
    control_reply_text(fd, 504, "Gateway Timeout",
                       "the guest did not reach an input poll within 10s, so "
                       "no pickup was read\n");
    return;
  }
  control_reply_json(fd, 200, "OK", request.body.c_str(), request.body.size());
}

} // namespace x2::control
