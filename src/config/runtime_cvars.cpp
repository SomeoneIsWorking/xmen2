/* The port's runtime CVar definitions and one init entry point. See
 * runtime_cvars.h for the layering contract. */
#include "runtime_cvars.h"

#include "config_directory.h"

#include <lucent/cvar.hpp>
#include <lucent/log_c.h>

#include <cstring>
#include <string>

namespace {

/* off: x86port re-translates every block instead of caching -- the
 * self-modifying-code / stale-block discriminator. */
lucent::cvar::Var<bool> g_hud_verify{"hud.verify", false};
lucent::cvar::Var<bool> g_hud_trace{"hud.trace", false};
lucent::cvar::Var<bool> g_jit_cache{"jit.cache", true};

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
 * output or state mismatch. The differential proof for the native vertex
 * builder; off in normal play. */
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

/* on: eligible native imports (_ftol, _stricmp, QPC, etc.) run directly from
 * the JIT dispatch loop on x86port CPU state. off uses the ordinary native
 * import dispatch. */
lucent::cvar::Var<bool> g_x86_import_fastpath{"engine.import_fastpath", true};

/* >0: arm x86port's block-entry histogram, sized for that many distinct block
 * addresses; the hottest blocks print at shutdown. A diagnostic -- it answers
 * "where does in-game guest time go" now that the crossing cost is gone. */
lucent::cvar::Var<long> g_jit_profile{"jit.profile", 0};

/* The JIT code arena's two independent limits, in blocks and in megabytes.
 * Either can be the one that binds, and which one it is decides what to fix,
 * so they are two knobs rather than one. 0 or negative keeps the built-in
 * default for the host. They exist because the game's working set of
 * translated blocks has never been measured: the browser evicts a block for
 * almost every block it translates (issue #161), and finding the size that
 * stops it needs runs past it rather than a rebuild per guess. */
lucent::cvar::Var<long> g_jit_blocks{"jit.blocks", 0};
lucent::cvar::Var<long> g_jit_code_mb{"jit.code_mb", 0};

/* >0: arm the hot-entry-point probe for that many entry points; the heartbeat
 * then prints how the frame's wall time splits between host imports and guest
 * bodies. A diagnostic, and a registered knob rather than a raw environment
 * read so that a platform with no environment reaches it the same way every
 * other knob is reached: --set hotep=4096 in the browser, or X2_HOTEP=4096 on
 * a host that has one. */
lucent::cvar::Var<long> g_hotep{"hotep", 0};

/* >0: every N windowed presents, photograph the frame that reaches the REAL
 * WINDOW through the shipping capture owner and report its mean/max luma and
 * non-black fraction. The one instrument that separates "the browser presents
 * black" from "a page screenshot cannot see a worker-owned canvas" -- headless
 * X2_SHOT refuses windowed runs, so this is the windowed counterpart
 * (docs/web-release.md). A diagnostic; --set present_luma=N in the browser,
 * or X2_PRESENT_LUMA=N on a host that has an environment. */
lucent::cvar::Var<long> g_present_luma{"present_luma", 0};

/* on: compare shared Alchemy input snapshots with the retained DirectInput
 * publication while the first shared-engine adapter is being qualified. */
lucent::cvar::Var<bool> g_alchemy_input_verify{"alchemy.input.verify", true};

/* Player/runtime policy. These are registered settings, not arbitrary
 * process-environment lookups. Their underscore names preserve the existing
 * X2_* developer overrides through Lucent's typed configuration layering. */
lucent::cvar::Var<bool> g_native_fmv{"native_fmv", true};
lucent::cvar::Var<bool> g_prompt_glyphs{"prompt_glyphs", true};
lucent::cvar::Var<std::string> g_text_scale{"text_scale", ""};
lucent::cvar::Var<std::string> g_boot_map{"boot_map", ""};
lucent::cvar::Var<bool> g_unpaced{"unpaced", false};
lucent::cvar::Var<bool> g_unbounded{"unbounded", false};
lucent::cvar::Var<long> g_quantum{"quantum", 20000};
lucent::cvar::Var<long> g_physical_memory_mb{"phys_mb", 512};
lucent::cvar::Var<std::string> g_movie_spin{"spin", ""};
lucent::cvar::Var<std::string> g_virtual_pad{"virtual_pad", ""};
lucent::cvar::Var<std::string> g_virtual_pad_id{"virtual_pad_id", ""};

void apply_set_token(const char *token) {
  const char *eq = std::strchr(token, '=');
  if (eq == nullptr || eq == token) {
    lucent_log_error("config", "--set expects NAME=VALUE, got '%s'", token);
    return;
  }
  lucent::cvar::set_arg(std::string(token, static_cast<size_t>(eq - token)),
                        eq + 1);
}

} // namespace

void x2_runtime_config_init(int argc, char **argv) {
  lucent::cvar::set_prefix("X2_");
  lucent::cvar::register_var(g_hud_verify);
  lucent::cvar::register_var(g_hud_trace);
  lucent::cvar::register_var(g_jit_cache);
  lucent::cvar::register_var(g_jit_inline_dispatch);
  lucent::cvar::register_var(g_jit_profile);
  lucent::cvar::register_var(g_jit_blocks);
  lucent::cvar::register_var(g_jit_code_mb);
  lucent::cvar::register_var(g_hotep);
  lucent::cvar::register_var(g_present_luma);
  lucent::cvar::register_var(g_x86_import_fastpath);
  lucent::cvar::register_var(g_alchemy_input_verify);
  lucent::cvar::register_var(g_audio_adpcm_verify);
  lucent::cvar::register_var(g_gfx_vtx_swizzle_verify);
  lucent::cvar::register_var(g_gfx_vtx_builder_verify);
  lucent::cvar::register_var(g_sg_attr_stack);
  lucent::cvar::register_var(g_sg_attr_stack_verify);
  lucent::cvar::register_var(g_audio_channel_poll);
  lucent::cvar::register_var(g_audio_channel_poll_verify);
  lucent::cvar::register_var(g_native_fmv);
  lucent::cvar::register_var(g_prompt_glyphs);
  lucent::cvar::register_var(g_text_scale);
  lucent::cvar::register_var(g_boot_map);
  lucent::cvar::register_var(g_unpaced);
  lucent::cvar::register_var(g_unbounded);
  lucent::cvar::register_var(g_quantum);
  lucent::cvar::register_var(g_physical_memory_mb);
  lucent::cvar::register_var(g_movie_spin);
  lucent::cvar::register_var(g_virtual_pad);
  lucent::cvar::register_var(g_virtual_pad_id);

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
