/* CHud's party draw is reached only when its visibility owner permits it. */
#include "touch_hud_runtime.h"
#include "../input/gameplay_control.h"
#include "guest_clock.h"
#include "hud_draw_runtime.h"
#include "x86rt_native.h"
#include <lucent/log_c.h>

static unsigned long g_root_calls;

static void hud_draw(CPU *cpu) {
  ++g_root_calls;
  x2_gameplay_control_hud_drawn(guest_clock_now_s());
  x2_hud_party_draw(cpu);
  if (g_root_calls % 300 == 0)
    x2_hud_draw_report();
}

void x2_touch_hud_report(void) {
  lucent_log_info("hud", "%lu visible party draws; control gate %s",
                  g_root_calls,
                  x2_gameplay_control_name(
                      (int)x2_gameplay_control_state(guest_clock_now_s())));
  x2_hud_draw_report();
}

__attribute__((constructor)) static void register_hud_draw(void) {
  x86_register_override("XMen2.exe", 0x005a43d0u, hud_draw);
}
