#include "guest_inspect.h"

#include "guest_memory.h"
#include "x86rt_native.h"

#include <lucent/log_c.h>

/* Bounded so a report stays readable and cannot flood the log of a wedged
   run: 256 words is 1 KiB of stack, well past any frame this is used on. */
enum { kGuestInspectMaxWords = 256u };

static unsigned bounded(unsigned words) {
  if (words == 0u) {
    return 1u;
  }
  return words > kGuestInspectMaxWords ? kGuestInspectMaxWords : words;
}

static int word_at(uint32_t address, uint32_t *out) {
  return guest_memory_try_read(address, out, sizeof *out);
}

static void dump_rows(const char *tag, uint32_t address, unsigned words) {
  unsigned i;
  for (i = 0; i < words; i += 8u) {
    uint32_t row[8];
    unsigned have = 0u;
    unsigned j;
    for (j = 0; j < 8u && i + j < words; ++j) {
      if (!word_at(address + (i + j) * 4u, &row[j])) {
        break;
      }
      have++;
    }
    if (have == 0u) {
      lucent_log_error("engine", "%s   +%02x: not readable", tag, (i * 4u));
      return;
    }
    lucent_log_error("engine",
                     "%s   +%02x: %08x %08x %08x %08x %08x %08x %08x %08x", tag,
                     i * 4u, have > 0u ? row[0] : 0u, have > 1u ? row[1] : 0u,
                     have > 2u ? row[2] : 0u, have > 3u ? row[3] : 0u,
                     have > 4u ? row[4] : 0u, have > 5u ? row[5] : 0u,
                     have > 6u ? row[6] : 0u, have > 7u ? row[7] : 0u);
  }
}

void guest_inspect_words(uint32_t address, unsigned words, const char *tag) {
  uint32_t probe;
  words = bounded(words);
  if (!word_at(address, &probe)) {
    lucent_log_error("engine",
                     "%s 0x%08x is not readable guest memory, so "
                     "there is nothing to show there",
                     tag, address);
    return;
  }
  lucent_log_error("engine", "%s %u word(s) at 0x%08x:", tag, words, address);
  dump_rows(tag, address, words);
}

void guest_inspect_stack(uint32_t esp, unsigned words, const char *tag) {
  unsigned named = 0u;
  unsigned i;
  words = bounded(words);
  guest_inspect_words(esp, words, tag);
  for (i = 0; i < words; ++i) {
    uint32_t value;
    uint32_t first;
    X86Module *module;
    if (!word_at(esp + i * 4u, &value)) {
      break;
    }
    module = x86_module_for(value);
    if (module) {
      named++;
      lucent_log_error("engine", "%s   +%02x %08x -> %s + 0x%08x", tag, i * 4u,
                       value, module->name,
                       module->preferred + (value - *module->base));
      continue;
    }
    /* A pointer whose first word is a code address is what an object with a
       vtable looks like, and naming the vtable names the type. This is the
       only way the object a failure came from appears at all: it is in no
       image itself. */
    if (!word_at(value, &first)) {
      continue;
    }
    module = x86_module_for(first);
    if (!module) {
      continue;
    }
    named++;
    lucent_log_error("engine",
                     "%s   +%02x %08x -> an object whose first word is "
                     "%s + 0x%08x, so it carries a vtable",
                     tag, i * 4u, value, module->name,
                     module->preferred + (first - *module->base));
  }
  lucent_log_error("engine",
                   "%s %u of %u word(s) could be named; the rest are data, "
                   "and they are printed above whether or not they could be",
                   tag, named, words);
}
