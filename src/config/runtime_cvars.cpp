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

/* on: an interception point that lands on host code this dispatcher owns (an
 * import thunk or a resolved native override body) is serviced inline and the
 * JIT run carries on, instead of unwinding x86p_jit_engine_run and being
 * re-entered per crossing. off restores the unwind-per-crossing path -- the
 * A/B for measuring the inline handler's effect. */
lucent::cvar::Var<bool> g_jit_inline_dispatch{"jit.inline_dispatch", true};

/* on: after each native IMA ADPCM decode (XMen2.exe 0x00616770 / 0x00616880),
 * re-run the guest's own body from the same start state and abort on any
 * output or state mismatch. The differential proof for the native decoder;
 * off in normal play. */
lucent::cvar::Var<bool> g_audio_adpcm_verify{"audio.adpcm_verify", false};

/* on: after each native vertex colour-channel swap (libIGGfx.dll 0x10046ce0),
 * re-run the guest's own body from the same start state and abort on any
 * output or state mismatch. The differential proof for the native swizzle;
 * off in normal play. */
lucent::cvar::Var<bool> g_gfx_vtx_swizzle_verify{"gfx.vtx_swizzle_verify",
                                                 false};

/* on: after each native CDxImmediateBuilder::addVertex (XMen2.exe 0x005840a0),
 * re-run the guest's own body from the same start state and abort on any
 * output or state mismatch. The differential proof for the native vertex builder;
 * off in normal play. */
lucent::cvar::Var<bool> g_gfx_vtx_builder_verify{"gfx.vtx_builder_verify",
                                                 false};

/* on: fast-path native override for igAttrStackManager::reset and
 * igAttrStack::customReset (libIGSg.dll 0x10034d30 / 0x10034d10).
 * off restores full guest JIT execution of the attribute stack resets. */
lucent::cvar::Var<bool> g_sg_attr_stack{"sg.attr_stack", true};

/* on: after each native igAttrStackManager::reset (libIGSg.dll 0x10034d30),
 * re-run the guest's own body from the same start state and abort on any
 * output or state mismatch. The differential proof for the native attr stack;
 * off in normal play. */
lucent::cvar::Var<bool> g_sg_attr_stack_verify{"sg.attr_stack_verify", false};

/* on: native override for the per-frame audio channel poll (XMen2.exe
 * 0x00594500). off restores full guest JIT execution of the 24-channel
 * completion sweep. */
lucent::cvar::Var<bool> g_audio_channel_poll{"audio.channel_poll", true};

/* on: after each native audio channel poll, run the guest body, capture its
 * memory effects, rewind, and abort on any divergence. off in normal play. */
lucent::cvar::Var<bool> g_audio_channel_poll_verify{"audio.channel_poll_verify",
                                                    false};

/* on: pure leaf import thunks (_ftol, _stricmp, QPC, etc.) bypass the substrate
 * callout bridge and run directly from the host dispatch loop on the
 * x86port CPU state. off restores the full substrate callout bridge. */
lucent::cvar::Var<bool> g_engine_leaf_thunks{"engine.leaf_thunks", true};

/* >0: arm x86port's block-entry histogram, sized for that many distinct block
 * addresses; the hottest blocks print at shutdown. A diagnostic -- it answers
 * "where does in-game guest time go" now that the crossing cost is gone. */
lucent::cvar::Var<long> g_jit_profile{"jit.profile", 0};

void apply_set_token(const char *token) {
  const char *eq = std::strchr(token, '=');
  if (eq == nullptr || eq == token) {
    std::fprintf(stderr, "x2native: --set expects NAME=VALUE, got '%s'\n",
                 token);
    return;
  }
  lucent::cvar::set_arg(std::string(token, static_cast<size_t>(eq - token)),
                        eq + 1);
}

} // namespace

void x2_runtime_config_init(int argc, char **argv) {
  lucent::cvar::set_prefix("X2_");
  lucent::cvar::register_var(g_engine);
  lucent::cvar::register_var(g_jit_cache);
  lucent::cvar::register_var(g_jit_verify);
  lucent::cvar::register_var(g_jit_inline_dispatch);
  lucent::cvar::register_var(g_jit_profile);
  lucent::cvar::register_var(g_engine_leaf_thunks);
  lucent::cvar::register_var(g_audio_adpcm_verify);
  lucent::cvar::register_var(g_gfx_vtx_swizzle_verify);
  lucent::cvar::register_var(g_gfx_vtx_builder_verify);
  lucent::cvar::register_var(g_sg_attr_stack);
  lucent::cvar::register_var(g_sg_attr_stack_verify);
  lucent::cvar::register_var(g_audio_channel_poll);
  lucent::cvar::register_var(g_audio_channel_poll_verify);

  const std::string path =
      std::string(x2_config_directory()) + "/x2native-runtime.conf";
  lucent::cvar::load_file(path.c_str());

  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--set") == 0 && i + 1 < argc)
      apply_set_token(argv[++i]);
    else if (std::strncmp(argv[i], "--set=", 6) == 0)
      apply_set_token(argv[i] + 6);
  }
}
