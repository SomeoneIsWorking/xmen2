/*
 * The hot-block report has to be able to say the thing nobody plans for.
 *
 * It is armed by a cvar and printed by the heartbeat, and its useful output is
 * a forty-row table. The failure that costs a session is the OTHER one: armed,
 * running, and recording nothing -- which without a deliberate sentence looks
 * exactly like a heartbeat that has not reached the report yet. A real run
 * does not produce it, so only a test can.
 *
 * `x86_engine_report_hot_blocks_from` is the function the product calls, one
 * adapter below the pool. The histogram is x86port's own, built here the way
 * the engine builds it, so nothing about it is reimplemented in the test.
 *
 * The pool and guest-memory sides of this translation unit are stubbed rather
 * than linked: standing up an engine, a memory map and a guest image to watch
 * a table print a sentence would test the engine instead of the report.
 */
#include "x86_engine_dispatch_report.h"

#include "x86rt_native.h"

#include <lucent/log.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::string> g_lines;
int g_failures;

bool said(const char *needle) {
  for (const std::string &line : g_lines) {
    if (line.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void check(bool condition, const char *what) {
  if (!condition) {
    std::printf("FAIL %s\n", what);
    for (const std::string &line : g_lines) {
      std::printf("     | %s\n", line.c_str());
    }
    g_failures++;
  }
}

} // namespace

/* The report asks for a name and must print something when there is none. */
const char *x86_native_name_at(uint32_t address) {
  return address == 0x00401000u ? "igNamedThing" : nullptr;
}

const X86pJitEngine *x86_engine_jit_pool_primary(const X86EngineJitPool *pool) {
  (void)pool;
  return nullptr;
}

int main() {
  lucent::set_sink([](lucent::Level, std::string_view line) {
    g_lines.push_back(std::string(line));
  });

  /* Not armed at all: the arming decision is reported once at startup, so a
     heartbeat repeating it every five seconds would bury what changes. */
  g_lines.clear();
  x86_engine_report_hot_blocks_from(nullptr, "[HB] ");
  check(g_lines.empty(), "an unarmed histogram prints nothing");

  X86pJitProfile *profile = x86p_jit_profile_create(64u);
  check(profile != nullptr, "the profile could be created");
  if (!profile) {
    return 1;
  }

  /* Armed and empty. The whole point of the file. */
  g_lines.clear();
  x86_engine_report_hot_blocks_from(profile, "[HB] ");
  check(g_lines.size() == 1, "armed and empty says exactly one thing");
  check(said("armed and has recorded no block entry"),
        "armed and empty names itself rather than printing a bare table head");

  /* Armed with entries: the ordinary case, and the proof that the sentence
     above is a branch and not the only thing this can say. */
  x86p_jit_profile_hit(profile, 0x00401000u);
  x86p_jit_profile_hit(profile, 0x00401000u);
  x86p_jit_profile_hit(profile, 0x00402000u);
  g_lines.clear();
  x86_engine_report_hot_blocks_from(profile, "[HB] ");
  check(!said("recorded no block entry"),
        "a populated histogram does not claim to be empty");
  check(said("2 distinct, 3 entries total"), "the header counts what went in");
  check(said("0x00401000"), "the hottest block is listed");
  check(said("igNamedThing"), "a block with a symbol is named");
  check(said("0x00402000"), "the second block is listed");
  check(said("unnamed"), "a block without a symbol still gets a row");
  check(g_lines.size() == 3, "one header and one row per distinct block");

  /* The tag is what tells a running snapshot from the final one. */
  g_lines.clear();
  x86_engine_report_hot_blocks_from(profile, "");
  check(g_lines.size() == 3, "the untagged form prints the same rows");
  check(!said("[HB] "), "the untagged form carries no heartbeat prefix");

  x86p_jit_profile_destroy(profile);
  std::printf("engine hot blocks: %d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
