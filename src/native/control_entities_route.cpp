#include "control_entities_route.hpp"

#include "control.h"
#include "control_command_bridge.h"
#include "control_query.h"
#include "guest_memory_view.hpp"
#include "json_string.h"
#include "retail_entities.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace x2::control {
namespace {

struct EntitiesRequest {
  std::string match;
  std::string body;
};

std::string lowered(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

std::string json_text(const std::string &value) {
  std::vector<char> buffer(value.size() * 6u + 3u);
  x2::native::json_string_format(buffer.data(), buffer.size(), value.c_str());
  return buffer.data();
}

void read_entities(void *context) {
  EntitiesRequest &request = *static_cast<EntitiesRequest *>(context);
  const native::LiveGuestMemory memory;
  std::string &body = request.body;
  body = "{\"entities\":[";
  bool first = true;
  for (const native::EntityView &entity :
       native::all_entities(memory, native::live_entity_tables())) {
    if (lowered(entity.definition).find(request.match) == std::string::npos) {
      continue;
    }
    std::array<char, 128> numbers{};
    std::snprintf(numbers.data(), numbers.size(),
                  "\"handle\":%u,\"x\":%.1f,\"y\":%.1f,\"z\":%.1f",
                  entity.handle, static_cast<double>(entity.at.x),
                  static_cast<double>(entity.at.y),
                  static_cast<double>(entity.at.z));
    body += first ? "{" : ",{";
    first = false;
    body += "\"definition\":" + json_text(entity.definition) + "," +
            numbers.data() + "}";
  }
  body += "]}\n";
}

} // namespace

void entities_route(x2::native::Socket fd, const char *query) {
  EntitiesRequest request;
  std::array<char, 128> match{};
  if (control_query_arg(query, "match", match.data(), match.size())) {
    request.match = lowered(match.data());
  }
  if (control_command_guest_read(read_entities, &request) < 0) {
    control_reply_text(fd, 504, "Gateway Timeout",
                       "the guest did not reach an input poll within 10s, so "
                       "no entity was read\n");
    return;
  }
  control_reply_json(fd, 200, "OK", request.body.c_str(), request.body.size());
}

} // namespace x2::control
