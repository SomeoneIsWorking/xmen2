#include "x2_log.h"

#include <lucent/log.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Entry {
  lucent::Level level;
  std::string line;
};

} // namespace

int main() {
  std::vector<Entry> entries;
  lucent::set_sink([&entries](lucent::Level level, std::string_view line) {
    entries.push_back({level, std::string(line)});
  });

  x2_log_info("ready %d\n", 7);
  x2_log_error("first line\nsecond line\n");
  lucent::set_sink(nullptr);

  if (entries.size() != 2u || entries[0].level != lucent::Level::Info ||
      entries[1].level != lucent::Level::Error ||
      entries[0].line.find("[x2] ready 7") == std::string::npos ||
      entries[1].line.find("[x2:error] first line second line") ==
          std::string::npos ||
      entries[0].line.find('\n') != std::string::npos ||
      entries[1].line.find('\n') != std::string::npos) {
    std::fprintf(stderr,
                 "test_x2_log: configurable one-line sink contract failed "
                 "(%zu entries)\n",
                 entries.size());
    for (const Entry &entry : entries)
      std::fprintf(stderr, "  level=%u line=%s\n",
                   static_cast<unsigned>(entry.level), entry.line.c_str());
    return 1;
  }
  return 0;
}
