/* The port's runtime CVar definitions and one init entry point. See
 * runtime_cvars.h for the layering contract. */
#include "runtime_cvars.h"

#include "config_directory.h"

#include <lucent/cvar.hpp>
#include <lucent/log_c.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

/* on: native overrides for libIGSg.dll's bounding-box frustum test -- the
 * clip-space box corners (0x10047570), their classification (0x100478e0) and
 * the driver that calls both (0x10047470), box_cull.c. off restores full
 * guest JIT execution of all three. */
lucent::cvar::Var<bool> g_sg_box_cull{"sg.box_cull", true};

/* on: after each native box_cull answer, re-run the guest's own body from the
 * same start state and abort on any difference in the output, EAX, ESP or x87
 * control state. The differential proof for box_cull.c; off in normal play. */
lucent::cvar::Var<bool> g_sg_box_cull_verify{"sg.box_cull_verify", false};

/* on: native overrides for libIGMath.dll's SSE vertex skinning -- weighted
 * blend (0x10022df0) and one bone per vertex (0x10022e80), skin.c. off
 * restores full guest JIT execution of both. */
lucent::cvar::Var<bool> g_math_skin{"math.skin", true};

/* on: after each native skinning answer, re-run the guest's own body from the
 * same start state and abort on any difference in the output vertices, the
 * argument slots it writes, ESP, EAX, ECX or EDX. The differential proof for
 * skin.c; off in normal play. */
lucent::cvar::Var<bool> g_math_skin_verify{"math.skin_verify", false};

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

/*
 * THE TWO GUEST-MEMORY WATCHES, registered rather than read from the
 * environment.
 *
 * `guest_watch=<addr>` reports the running body each time that address is
 * read; `write_watch=<addr>[:<value>]` reports who WRITES it, and the optional
 * value narrows a hot slot to "who writes zero here", which is usually the
 * question. Issue #172 is exactly that question -- a Cg hash table whose step
 * count is zero on the emulator and ten on the desktop -- and the diagnostic
 * could not be armed where the bug reproduces: a packaged Android app has no
 * environment for X2_WRITE_WATCH to live in. Lucent's X2_ prefix still answers
 * for a desktop shell, and the same name now works in the runtime conf an
 * Android run reads and in --set.
 */
lucent::cvar::Var<std::string> g_guest_watch{"guest_watch", ""};
lucent::cvar::Var<std::string> g_write_watch{"write_watch", ""};

/*
 * A NAME NOTHING ANSWERS TO IS A REFUSAL, NOT A SETTING.
 *
 * lucent stashes an override for a CVar that has not registered yet, because
 * a file is read before the consumer's variables exist. Every one of this
 * port's variables is registered below, before the command line is walked, so
 * a --set name that is still unclaimed at that point is one no run will ever
 * answer to -- a typo, or a key belonging to the OTHER configuration system
 * (the player settings in x2native.conf, whose names are read by
 * x2_settings_store and never by a CVar).
 *
 * Measured: `--set input.touch_controls=2` was accepted in silence and did
 * nothing, and the run it produced -- the overlay's pad attached too late for
 * the guest to enumerate it -- was read as evidence about touch for as long
 * as it took to notice the setting had not applied. A maintainer switch that
 * quietly does nothing turns a measurement into a story.
 */
bool name_is_registered(const std::string &name) {
  bool found = false;
  lucent::cvar::enumerate([&](lucent::cvar::VarBase &var) {
    if (var.name() == name) {
      found = true;
    }
  });
  return found;
}

void report_unknown_name(const std::string &name) {
  std::vector<std::string> names;
  lucent::cvar::enumerate(
      [&](lucent::cvar::VarBase &var) { names.push_back(var.name()); });
  std::sort(names.begin(), names.end());
  std::string known;
  for (const std::string &each : names) {
    known += known.empty() ? "" : " ";
    known += each;
  }
  lucent_log_error("config",
                   "--set %s: no such setting. The player settings in "
                   "x2native.conf are a different system and are not "
                   "reachable here. Known: %s",
                   name.c_str(), known.c_str());
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
  lucent::cvar::register_var(g_sg_box_cull);
  lucent::cvar::register_var(g_sg_box_cull_verify);
  lucent::cvar::register_var(g_math_skin);
  lucent::cvar::register_var(g_math_skin_verify);
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
  lucent::cvar::register_var(g_guest_watch);
  lucent::cvar::register_var(g_write_watch);

  const std::string path =
      std::string(x2_config_directory()) + "/x2native-runtime.conf";
  lucent::cvar::load_file(path.c_str());

  int refused = 0;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--set") == 0 && i + 1 < argc) {
      refused += !x2_runtime_config_apply_set_token(argv[++i]);
    } else if (std::strncmp(argv[i], "--set=", 6) == 0) {
      refused += !x2_runtime_config_apply_set_token(argv[i] + 6);
    }
  }
  /* Every bad name is named first, then the launch stops: a maintainer who
     mistyped two of them should not have to relaunch twice to learn so. */
  if (refused != 0) {
    std::exit(2);
  }
}

int x2_runtime_config_apply_set_token(const char *token) {
  const char *eq = token != nullptr ? std::strchr(token, '=') : nullptr;
  if (eq == nullptr || eq == token) {
    lucent_log_error("config", "--set expects NAME=VALUE, got '%s'",
                     token != nullptr ? token : "");
    return 0;
  }
  const std::string name(token, static_cast<size_t>(eq - token));
  if (!name_is_registered(name)) {
    report_unknown_name(name);
    return 0;
  }
  lucent::cvar::set_arg(name, eq + 1);
  return 1;
}
