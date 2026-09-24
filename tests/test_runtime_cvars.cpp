/*
 * --set NAMES A REGISTERED SETTING, OR THE LAUNCH IS REFUSED.
 *
 * lucent stashes an override whose CVar has not registered yet, so an
 * unknown name used to be kept forever and applied to nothing. Measured:
 * `--set input.touch_controls=2` -- a key belonging to the player settings
 * file, not to this system -- was accepted in silence, and the run it
 * produced was read as evidence about touch until the setting's absence was
 * noticed by hand.
 *
 * This drives the same function the command line drives, so the refusal is
 * provable without running a process to its exit status.
 */
#include "runtime_cvars.h"

#include <lucent/cvar_c.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace {

int g_failures;

void check(bool ok, const std::string &what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    g_failures++;
  }
}

/* The test's own config directory, removed however main returns: one per run
   under scratch/ otherwise accumulates without bound. */
class ScratchDirectory {
public:
  explicit ScratchDirectory(const char *path) : path_(path) {}
  ScratchDirectory(const ScratchDirectory &) = delete;
  ScratchDirectory &operator=(const ScratchDirectory &) = delete;
  ~ScratchDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
    if (error) {
      std::fprintf(stderr, "could not remove %s: %s\n", path_.c_str(),
                   error.message().c_str());
    }
  }

private:
  std::string path_;
};

} // namespace

int main() {
  /* Its own directory, so the conf file init reads is whatever this test put
     there -- never the machine's real one. */
  char config[] = "scratch/runtime-cvars-XXXXXX";
  if (mkdtemp(config) == nullptr) {
    std::perror("mkdtemp");
    return 1;
  }
  const ScratchDirectory owned(config);
  setenv("XDG_CONFIG_HOME", config, 1);

  char program[] = "test_runtime_cvars";
  char *argv[] = {program, nullptr};
  x2_runtime_config_init(1, argv);

  check(x2_runtime_config_apply_set_token("jit.watchn=7") != 0,
        "a registered name is accepted");
  check(lucent_cvar_number("jit.watchn", 0) == 7,
        "and the value it carried is the one the run reads");

  check(x2_runtime_config_apply_set_token("input.touch_controls=2") == 0,
        "a player-settings key belongs to the other system and is refused");
  check(x2_runtime_config_apply_set_token("jit.wtachn=7") == 0,
        "a transposed name is refused rather than stashed");
  check(x2_runtime_config_apply_set_token("jit.watchn") == 0,
        "a token with no value is refused");
  check(x2_runtime_config_apply_set_token("=7") == 0,
        "a token with no name is refused");

  /* The refusals must not have moved anything: a name nothing answers to
     cannot be allowed to land on the nearest thing that does. */
  check(lucent_cvar_number("jit.watchn", 0) == 7,
        "a refused token changes no registered value");

  if (g_failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("runtime_cvars: --set refuses what nothing answers to\n");
  return 0;
}
