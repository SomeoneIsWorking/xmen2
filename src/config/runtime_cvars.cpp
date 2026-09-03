/* The port's runtime CVar definitions and one init entry point. See
 * runtime_cvars.h for the layering contract. */
#include "runtime_cvars.h"

#include "config_directory.h"

#include <lucent/cvar.hpp>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

/* The engine the outer dispatch loop resolves (x86_engine.c reads this via the
 * C ABI). Empty means "unset": x86p_engine_resolve then picks the build's
 * best available default (JIT when linked, else the interpreter), exactly as
 * an unset X2_ENGINE did before. A non-empty "substrate"|"interpreter"|"jit"
 * from the file, the X2_ENGINE env, or --set is honoured verbatim. */
lucent::cvar::Var<std::string> g_engine{"engine", ""};

/* off: x86port re-translates every block instead of caching -- the
 * self-modifying-code / stale-block discriminator. */
lucent::cvar::Var<bool> g_jit_cache{"jit.cache", true};

/* on: x86port runs the interpreter alongside each JIT block and aborts on the
 * first whole-machine divergence. */
lucent::cvar::Var<bool> g_jit_verify{"jit.verify", false};

void apply_set_token(const char *token) {
  const char *eq = std::strchr(token, '=');
  if (eq == nullptr || eq == token) {
    std::fprintf(stderr, "x2native: --set expects NAME=VALUE, got '%s'\n", token);
    return;
  }
  lucent::cvar::set_arg(std::string(token, static_cast<size_t>(eq - token)), eq + 1);
}

} // namespace

void x2_runtime_config_init(int argc, char **argv) {
  lucent::cvar::set_prefix("X2_");
  lucent::cvar::register_var(g_engine);
  lucent::cvar::register_var(g_jit_cache);
  lucent::cvar::register_var(g_jit_verify);

  const std::string path = std::string(x2_config_directory()) + "/x2native-runtime.conf";
  lucent::cvar::load_file(path.c_str());

  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--set") == 0 && i + 1 < argc)
      apply_set_token(argv[++i]);
    else if (std::strncmp(argv[i], "--set=", 6) == 0)
      apply_set_token(argv[i] + 6);
  }
}
