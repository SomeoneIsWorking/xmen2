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

/* >0: arm x86port's runtime chain census, sized for that many distinct block
 * addresses. It answers issue #166's open question: of the dispatches the run
 * actually paid, how many went to an address the block just left had already
 * emitted as a constant -- the population general block chaining removes, and
 * a strictly bigger question than the static exit counts. Prints at shutdown.
 */
lucent::cvar::Var<long> g_jit_chain{"jit.chain", 0};

/* >0: the guest address whose first few entries report how the run reached
 * them -- the block just left, the register file on arrival, and what this
 * thread last crossed into. `jit.watchn` bounds the reports and defaults to 4,
 * because the address this exists for (issue #158's `JMP $` at 0x00403210) is
 * entered about twenty million times a second and reporting every entry would
 * be the stall. A diagnostic; the profile says which block is hot, this says
 * how the run got there. */
lucent::cvar::Var<long> g_jit_watch{"jit.watch", 0};
lucent::cvar::Var<long> g_jit_watch_reports{"jit.watchn", 4};
/* One guest address a watch report should also dump, and how many words of it.
 * The report names the pointers it finds on the stack; this is the run after
 * that, following one of them. Zero asks for nothing.
 *
 * REGISTERED BECAUSE THEY ARE READ. x86_engine_jit_diag.c read both names
 * unconditionally on every watch report, and an unregistered name is refused
 * -- so arming jit.watch killed the run on its first report, which is the one
 * thing a diagnostic must never do to what it is diagnosing. */
/* The maintainer control channel's loopback port, 0 for none.
 *
 * A CVar and not a bare environment read, because an Android package has no
 * environment: this is how a device run asks for the channel, through the same
 * runtime conf the JIT diagnostics use. `--control=N` still outranks it. It is
 * OFF by default and the product never turns it on -- see control_start. */
lucent::cvar::Var<long> g_control_port{"control.port", 0};
lucent::cvar::Var<long> g_jit_peek{"jit.peek", 0};
lucent::cvar::Var<long> g_jit_peek_words{"jit.peekn", 16};
lucent::cvar::Var<bool> g_x87_census{"x87.census", false};

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
/* The heartbeat's guest-memory peek. A registered knob rather than a raw
   environment read, so the browser -- which has no environment -- reaches it
   with --set peek=... exactly as a desktop reaches it with X2_PEEK=..., which
   Lucent's X2_ prefix still answers. */
lucent::cvar::Var<std::string> g_peek{"peek", ""};
/* The file-operation trace. Registered for the same reason as `peek`: a
   browser has no environment to put X2_FILES in, and the one place a stalled
   loader can be seen is what it asked the filesystem for. */
lucent::cvar::Var<bool> g_files{"files", false};
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
  lucent::cvar::register_var(g_jit_chain);
  lucent::cvar::register_var(g_jit_watch);
  lucent::cvar::register_var(g_jit_watch_reports);
  lucent::cvar::register_var(g_control_port);
  lucent::cvar::register_var(g_jit_peek);
  lucent::cvar::register_var(g_jit_peek_words);
  lucent::cvar::register_var(g_x87_census);
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
  lucent::cvar::register_var(g_peek);
  lucent::cvar::register_var(g_files);
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
