#include "live_session.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  failures += ok ? 0 : 1;
}

} // namespace

int main() {
  const std::filesystem::path dir = X2_TEST_LIVE_SESSION_DIR;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  /* Another run's record mid-write, under the name every run once shared. */
  std::filesystem::create_directory(dir / "live.json.new");
  live_session_set_directory(dir.c_str());

  check(live_session_start(8571, nullptr) == 1,
        "a run publishes while another run's record is mid-write");
  std::ifstream in(dir / "live.json");
  std::stringstream text;
  text << in.rdbuf();
  const std::string record = text.str();
  check(record.find("\"pid\": " + std::to_string(getpid())) !=
                std::string::npos &&
            record.find("\"control_port\": 8571") != std::string::npos,
        "the record names this run's pid and port");
  check(std::filesystem::is_directory(dir / "live.json.new"),
        "the other run's pending record is left alone");
  bool leftover = false;
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    leftover = leftover || (entry.path().filename() != "live.json" &&
                            entry.path().filename() != "live.json.new");
  }
  check(!leftover, "this run's own pending record was renamed into place");
  return failures == 0 ? 0 : 1;
}
