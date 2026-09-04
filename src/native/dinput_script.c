#include "../config/environment.h"
#include "x2_log.h"
/* Scripted keyboard input for deterministic, headless game runs. */
#include "dinput_device.h"
#include "dinput_script.h"
#include "guest_clock.h"
#include "guest_memory.h"

#include "dinput_fifo.h"
#include "dinput_system.h"
#include "gpu_device.h"
#include "x86rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

/*
 * X2_INPUT_SCRIPT accepts time- or frame-based presses:
 *
 *   4.0+150:Down f1200+20:Return f1400-1800/20:Escape
 *
 * Repeat windows cover nondeterministic movie decode time without pretending
 * an instant landed on the intended screen. Every press/release is reported.
 */
#define SCRIPT_MAX 128
typedef struct {
  double at, until;
  int by_frame;
  unsigned char dik;
  int down, said;
  char name[24];
} ScriptKey;

static ScriptKey g_script[SCRIPT_MAX];
static int g_nscript, g_script_parsed;
static double g_script_t0;

/* The guest's clock, not a private one: see guest_clock.h. Five copies of
   this read CLOCK_MONOTONIC directly, and the guest gates real logic on
   elapsed time, so any two of them disagreeing is a timing bug wearing a
   gameplay bug's clothes. */
static double script_now(void) { return guest_clock_now_s(); }

static void script_parse(void) {
  const char *value = x2_config_override_get(kX2ConfigInputScript);
  const char *p;

  g_script_parsed = 1;
  if (!value || !*value)
    return;
#ifndef X2_WITH_SDL
  x2_log_error("DINPUT8: X2_INPUT_SCRIPT is set but this build has no "
               "SDL, so no key name can be resolved and NOTHING will be "
               "injected.\n");
  return;
#else
  for (p = value; *p;) {
    double at = 0.0, hold = 100.0, to = 0.0, step = 0.0;
    char name[24];
    int consumed = 0, scancode, by_frame = 0, repeats = 1, r;
    while (*p == ' ' || *p == ',' || *p == '\t')
      p++;
    if (!*p)
      break;
    if (*p == 'f' || *p == 'F') {
      by_frame = 1;
      hold = 10.0;
      p++;
    }
    if (sscanf(p, "%lf-%lf/%lf+%lf:%23[^ ,\t]%n", &at, &to, &step, &hold, name,
               &consumed) == 5 ||
        (consumed = 0, sscanf(p, "%lf-%lf/%lf:%23[^ ,\t]%n", &at, &to, &step,
                              name, &consumed)) == 4) {
      if (step <= 0.0 || to < at) {
        x2_log_error("DINPUT8: X2_INPUT_SCRIPT has the repeat "
                     "\"%s\", whose window is empty (from %g to %g "
                     "every %g). Refusing rather than scheduling a "
                     "press that never happens.\n",
                     p, at, to, step);
        return;
      }
      repeats = (int)((to - at) / step) + 1;
    } else if (sscanf(p, "%lf+%lf:%23[^ ,\t]%n", &at, &hold, name, &consumed) !=
                   3 &&
               (consumed = 0,
                sscanf(p, "%lf:%23[^ ,\t]%n", &at, name, &consumed)) != 2) {
      x2_log_error("DINPUT8: X2_INPUT_SCRIPT could not be read at "
                   "\"%s\" -- expected <time>[+<hold>]:<key> or a "
                   "repeat window. NOTHING after it was scheduled.\n",
                   p);
      return;
    }
    p += consumed;
    if (g_nscript + repeats > SCRIPT_MAX) {
      x2_log_error("DINPUT8: X2_INPUT_SCRIPT needs %d more event(s) "
                   "for \"%s\" and only %d of %d slot(s) are left. "
                   "Refusing rather than cutting off the tail.\n",
                   repeats, name, SCRIPT_MAX - g_nscript, SCRIPT_MAX);
      return;
    }
    scancode = (int)SDL_GetScancodeFromName(name);
    if (scancode == SDL_SCANCODE_UNKNOWN) {
      x2_log_error("DINPUT8: X2_INPUT_SCRIPT names the key \"%s\", "
                   "which SDL does not know. Refusing.\n",
                   name);
      return;
    }
    {
      unsigned char dik = dinput_system_dik(scancode);
      if (!dik) {
        x2_log_error("DINPUT8: \"%s\" has no DirectInput DIK "
                     "mapping, so the game could never see it. "
                     "Refusing.\n",
                     name);
        return;
      }
      for (r = 0; r < repeats; r++) {
        ScriptKey *key = &g_script[g_nscript++];
        double event_at = at + (double)r * step;
        key->dik = dik;
        key->at = event_at;
        key->until = event_at + (by_frame ? hold : hold / 1000.0);
        key->by_frame = by_frame;
        snprintf(key->name, sizeof key->name, "%s", name);
      }
      if (repeats > 1)
        x2_log_error("DINPUT8: X2_INPUT_SCRIPT -- \"%s\" repeats "
                     "%d time(s), every %g %s from %g to %g.\n",
                     name, repeats, step, by_frame ? "frame(s)" : "second(s)",
                     at, at + (double)(repeats - 1) * step);
    }
  }
  if (g_nscript)
    x2_log_error("DINPUT8: X2_INPUT_SCRIPT -- %d scripted key press(es) "
                 "will be INJECTED; each is reported as it fires.\n",
                 g_nscript);
#endif
}

void dinput_script_start(void) {
  g_script_t0 = script_now();
  if (!g_script_parsed)
    script_parse();
  if (g_nscript)
    x2_log_error("DINPUT8: the script's clock starts NOW; its times are "
                 "seconds from here.\n");
}

void dinput_script_apply(CPU *cpu, uint32_t out, uint32_t size) {
  double now;
  int i;

  if (!g_script_parsed)
    script_parse();
  now = script_now();
  if (g_script_t0 == 0.0)
    g_script_t0 = now;
  now -= g_script_t0;
  dinput_fifo_apply(cpu, out, size, now);
  if (!g_nscript)
    return;
  for (i = 0; i < g_nscript; i++) {
    ScriptKey *key = &g_script[i];
    double when = key->by_frame ? (double)gpu_frames_presented() : now;
    int down = when >= key->at && when < key->until;
    if (down && (uint32_t)key->dik < size)
      *((unsigned char *)guest_memory_pointer(out) + key->dik) = 0x80;
    if (down && !key->down)
      x2_log_error("DINPUT8: INJECTING \"%s\" (DIK 0x%02x) at "
                   "t=%.2fs, frame %lu\n",
                   key->name, key->dik, now, gpu_frames_presented());
    else if (!down && key->down && !key->said++)
      x2_log_error("DINPUT8: released \"%s\" at t=%.2fs, frame %lu\n",
                   key->name, now, gpu_frames_presented());
    key->down = down;
  }
}
