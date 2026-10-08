#include "input_probe_lifecycle.h"

#include "dinput_pad.h"
#include "player_input.h"

#include <cstdarg>
#include <cstdio>

namespace x2::input {
namespace {

void append(char *out, std::size_t n, std::size_t *at, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

void append(char *out, std::size_t n, std::size_t *at, const char *fmt, ...) {
  std::va_list ap;
  int written;
  if (*at >= n)
    return;
  va_start(ap, fmt);
  written = std::vsnprintf(out + *at, n - *at, fmt, ap);
  va_end(ap);
  if (written > 0)
    *at += (std::size_t)written > n - *at ? n - *at : (std::size_t)written;
}

} // namespace

std::size_t input_probe_lifecycle_report(char *out, std::size_t n) {
  unsigned char guid[16];
  std::size_t at = 0;
  int pad, player;

  if (!out || !n)
    return 0;
  dinput_pad_refresh();
  append(out, n, &at, "controller inventory generation %llu: %d connected\n",
         (unsigned long long)dinput_pad_generation(), dinput_pad_count());
  for (pad = 0; pad < DINPUT_PAD_MAX; pad++) {
    const char *id;
    const char *name;
    if (!dinput_pad_instance_guid(pad, guid))
      continue;
    id = dinput_pad_persistent_id(pad);
    name = dinput_pad_name(pad);
    append(out, n, &at,
           "  slot %d live-guid %02x%02x%02x%02x%02x%02x%02x%02x"
           "%02x%02x%02x%02x%02x%02x%02x%02x identity %s (%s) name %s\n",
           pad, guid[0], guid[1], guid[2], guid[3], guid[4], guid[5], guid[6],
           guid[7], guid[8], guid[9], guid[10], guid[11], guid[12], guid[13],
           guid[14], guid[15], id ? id : "(none)",
           dinput_pad_persistent_id_is_stable(pad) ? "stable" : "session",
           name ? name : "(none)");
  }
  append(out, n, &at, "  resolved players:");
  for (player = 0; player < 4; player++) {
    const char *resolved = NULL;
    pad = x2_player_input_resolved_pad((unsigned)player);
    if (pad >= 0)
      resolved = dinput_pad_persistent_id(pad);
    append(out, n, &at, " P%d=%s", player + 1,
           resolved ? resolved : "keyboard/none");
  }
  append(out, n, &at, "\n\n");
  return at < n ? at : n - 1;
}

} // namespace x2::input
