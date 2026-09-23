/*
 * timer_accessor.c -- XMen2.exe's timer-singleton accessor (0x0055b610), the
 * override that owns frame pacing: X2_UNPACED's frame-cap write and the paced
 * limiter's sleep both happen at its call, and every other call answers the
 * singleton natively, in place of the CALL when it is direct
 * (override_leaf.h).
 */
#include "frame_limiter_wait.h"
#include "guest_body.h"
#include "override_leaf.h"
#include "threads.h"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <lucent/cvar_c.h>

#include <stdint.h>

/* ---------------------------------------------------------------------
 * X2_UNPACED -- run the frame loop as fast as it will go.
 *
 * The game paces itself: XMen2.exe's frame function stores a minimum frame
 * time (1/30 or 1/60, from a config query) into its app object at +0x18 at its
 * own top, and then busy-waits at 0x00401ff0 until that much has elapsed. That
 * is correct behaviour and it is what a player wants -- and it is exactly
 * wrong for a test, which spends twenty-five wall seconds to see twenty-five
 * seconds of game.
 *
 * So this zeroes the cap. With it at 0 the limiter's comparison is satisfied
 * on the first read and nothing else changes: the clock still advances at real
 * speed, so animation, physics and timers all see the time they actually took.
 * A frame-rate CAP is being removed, not time being scaled -- scaling the
 * clock would make a test that "passes at 10x" say nothing about the game.
 *
 * WHY HERE. The write has to land between the store at the top of the frame
 * and the limiter, and the only guest code that runs in that window and is
 * overridable is the limiter's own first instruction: CALL 0x0055b610, the
 * timer-singleton accessor. Hooking Present instead was tried and does
 * nothing, because Present happens LATER in the frame than the limiter, so the
 * value is overwritten before it is read -- the run stayed at exactly 60fps
 * and the message claiming otherwise was printing the whole time.
 *
 * The app object is a STATIC in the exe image (0x006f3ac4), resolved through
 * the module's mapped base rather than assumed, because the exe does not have
 * to land at its preferred address.
 */
#define APP_OBJECT_RVA 0x002f3ac4u /* 0x006f3ac4 - 0x00400000 */
#define APP_FRAME_CAP 0x18u        /* float, minimum seconds/frame */
#define APP_FRAME_START 0x1cu      /* float, the clock when the frame began */
/* The limiter loop's clock-read call, 0x00401ff0 CALL 0x0055b610, returns
   here; the loop's last clock read is at [esp+0x14] of its frame. See
   frame_limiter_wait.h. */
#define LIMITER_RETURN_RVA 0x00001ff5u /* 0x00401ff5 - 0x00400000 */
#define LIMITER_LAST_READ 0x14u

static uint32_t s_limiter_return;
static uint32_t s_app_object;

/* At the limiter's clock read, sleep through what its last read says is left
   of the frame, instead of spinning through it. */
static void frame_limiter_wait(const CPU *C) {
  const uint32_t frame_esp = C->reg[kX86pEsp] + 4u;
  const uint32_t ms =
      frame_limiter_sleep_ms((float)RDF32(s_app_object + APP_FRAME_CAP),
                             (float)RDF32(s_app_object + APP_FRAME_START),
                             (float)RDF32(frame_esp + LIMITER_LAST_READ));
  if (ms)
    guest_sleep_ms(ms);
}

/* The accessor's pacing state, found on its first call: -1 until then, then
   whether X2_UNPACED zeroes the frame cap. */
static int s_timer_unpaced = -1;
static uint32_t s_timer_frame_cap;
static uint32_t s_timer_guard;
static uint32_t s_timer_instance;

static void timer_accessor_init(void) {
  s_timer_unpaced = lucent_cvar_flag("unpaced", 0) != 0;
  X86Module *m;
  for (m = x86_modules(); m; m = m->next)
    if (m->preferred == 0x00400000u && *m->base)
      break;
  if (m) {
    s_timer_guard = *m->base + (0x007ac288u - 0x00400000u);
    s_timer_instance = *m->base + (0x007ac248u - 0x00400000u);
    s_app_object = *m->base + APP_OBJECT_RVA;
    s_limiter_return = *m->base + LIMITER_RETURN_RVA;
    if (s_timer_unpaced) {
      s_timer_frame_cap = *m->base + APP_OBJECT_RVA + APP_FRAME_CAP;
      x2_log_info("X2_UNPACED: the game's frame cap at 0x%08x is zeroed "
                  "before every clock read, so the frame loop runs as "
                  "fast as it can. The clock is NOT scaled -- everything "
                  "still sees real elapsed time.\n",
                  s_timer_frame_cap);
    }
  } else if (s_timer_unpaced) {
    x2_log_error("X2_UNPACED: the exe is not mapped, so the "
                 "frame cap could not be found. The run is "
                 "PACED, whatever the variable says.\n");
    s_timer_unpaced = 0;
  }
}

/* The paced limiter's own clock read, which sleeps before it returns. */
static int timer_call_is_limiter(const CPU *C) {
  return !s_timer_unpaced && s_limiter_return &&
         RD32(C->reg[kX86pEsp]) == s_limiter_return;
}

/* Once initialized, 0x0055b610 is a pure Meyers singleton getter returning the
   address of the global timer instance at 0x007ac248. Answering it natively
   avoids 2.8M guest SEH frame setups per 2000 frames. */
static int timer_instance_ready(CPU *C) {
  if (!s_timer_guard || !(RD8(s_timer_guard) & 1u))
    return 0;
  C->reg[kX86pEax] = s_timer_instance;
  C->reg[kX86pEsp] += 4u;
  return 1;
}

/* In place of a direct CALL: every call but the first, the limiter's sleep and
   the singleton's construction, none of which a leaf may run. */
static int timer_accessor_leaf(CPU *C) {
  if (s_timer_unpaced < 0 || timer_call_is_limiter(C))
    return 0;
  if (s_timer_unpaced)
    WRF32(s_timer_frame_cap, 0.0f);
  return timer_instance_ready(C);
}

void x2_override_0055b610(CPU *C) {
  if (__builtin_expect(s_timer_unpaced < 0, 0))
    timer_accessor_init();
  if (s_timer_unpaced)
    WRF32(s_timer_frame_cap, 0.0f);
  else if (timer_call_is_limiter(C))
    frame_limiter_wait(C);
  if (__builtin_expect(timer_instance_ready(C), 1))
    return;
  x86_guest_body(C, "XMen2.exe", 0x0055b610u);
}

__attribute__((constructor)) static void timer_accessor_register(void) {
  x86_register_override("XMen2.exe", 0x0055b610, x2_override_0055b610);
  x86_register_override_leaf("XMen2.exe", 0x0055b610, timer_accessor_leaf);
}
