/*
 * SetMaterial, SetLight and LightEnable, with the evidence for what the engine
 * asked of each.
 *
 * Split from d3d8_device.c because these three carry far more diagnostic than
 * device: the D3D8 side is three memcpys into the state mirror, and everything
 * else here exists to answer why a lit draw arrives black -- who called
 * SetLight, from where, with what, and which indices this device refused.
 *
 * The methods keep going through the device vtable; only the code moved.
 */
#include "d3d8_device_lights.h"

#include "d3d8_com.h"
#include "d3d8_device.h"
#include "d3d8_host.h"
#include "d3d8_lightlog.h"
#include "d3d8_state.h"
#include "d3d8_types.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void d3d8_dev_SetMaterial(D3D8Object *self, CPU *C) {
  const float *m = (const float *)d3d8_guest_ptr(d3d8_arg(C, 0), "material");
  (void)self;
  if (!m) {
    d3d8_ret(C, D3DERR_INVALIDCALL);
    return;
  }
  /*
   * X2_MATERIAL_DUMP=<n> -- what the ENGINE actually sets.
   *
   * The level renders black because every lit draw arrives with a material
   * whose diffuse is 0,0,0 and a vertex format with no colour to stand in
   * for it. That is either what the engine asked for, or a mirror of ours
   * that has drifted -- and those need opposite fixes, so the value is
   * printed AT THE CALL rather than inferred from the draw.
   *
   * A run where the count reaches n and every diffuse is zero is a real
   * answer; so is one where it never fires, which says SetMaterial is not
   * the path this engine uses.
   */
  {
    static long want = -2, done;
    if (want == -2) {
      const char *e = getenv("X2_MATERIAL_DUMP");
      want = (e && *e) ? atol(e) : -1;
    }
    if (want > 0 && done < want) {
      done++;
      fprintf(stderr,
              "d3d8 SetMaterial %ld/%ld: diffuse %.3f %.3f %.3f "
              "%.3f  ambient %.3f %.3f %.3f  emissive %.3f %.3f %.3f  "
              "power %.2f\n",
              done, want, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[12],
              m[13], m[14], m[16]);
    }
  }
  d3d8_lightlog("SETMATERIAL t=%lu diffuse=%.4f,%.4f,%.4f,%.4f "
                "ambient=%.4f,%.4f,%.4f,%.4f emissive=%.4f,%.4f,%.4f,%.4f "
                "specular=%.4f,%.4f,%.4f,%.4f power=%.2f",
                d3d8_lightlog_ms(), m[0], m[1], m[2], m[3], m[4], m[5], m[6],
                m[7], m[12], m[13], m[14], m[15], m[8], m[9], m[10], m[11],
                m[16]);
  memcpy(d3d8_device_state()->material, m,
         sizeof d3d8_device_state()->material);
  d3d8_device_state()->material_set = 1;
  d3d8_ret(C, D3D_OK);
}

/*
 * A light index this table cannot hold.
 *
 * The engine does not check SetLight's HRESULT -- nothing in D3D8 obliges it
 * to -- so a refusal here is INVISIBLE unless it is said. It was silent, and
 * with the table at 16 slots that silence swallowed 63% of the original
 * engine's SetLight calls (it uses indices up to 51, measured against the
 * stock game through tools/proxy_d3d8). Said by name the first few times, and
 * counted with its denominator in the report, so a light this device dropped
 * can never again look like a light the engine never set.
 */
static unsigned long g_light_idx_refused, g_light_idx_max_seen;
static unsigned long g_lightenable_calls;

static void light_index_refused(const char *what, uint32_t idx) {
  static int told;
  g_light_idx_refused++;
  if (idx > g_light_idx_max_seen)
    g_light_idx_max_seen = idx;
  if (told < 8) {
    told++;
    fprintf(stderr,
            "d3d8: %s(%u) -- this device holds %d light(s) and the "
            "engine asked for index %u. REFUSED; that light will "
            "not reach any draw.\n",
            what, idx, D3D8_MAX_LIGHTS, idx);
  }
}

/* SetLight call sites, for "who sets a black light" -- see dev_SetLight. */
#define SETLIGHT_SITES 16
static struct {
  uint32_t ra;
  unsigned long calls, black;
} g_setlight_site[SETLIGHT_SITES];
static int g_nsetlight_site;
static unsigned long g_setlight_calls, g_setlight_black, g_setlight_over;
/* What the LAST SetLight left in each slot -- see dev_SetLight. */
static struct {
  unsigned long calls, black;
  float last[3];
  uint32_t last_type;
} g_light_slot[D3D8_MAX_LIGHTS];
/* Whether the engine's own light-position transform did anything -- see
   dev_SetLight. */
static unsigned long g_light_transformed, g_light_untransformed,
    g_light_tail_unreadable;

void d3d8_dev_SetLight(D3D8Object *self, CPU *C) {
  uint32_t idx = d3d8_arg(C, 0);
  const float *l = (const float *)d3d8_guest_ptr(d3d8_arg(C, 1), "light");
  (void)self;
  /* Before the refusal, for the reason given at dev_LightEnable. */
  if (!l)
    d3d8_lightlog("SETLIGHT t=%lu idx=%lu NULL-POINTER", d3d8_lightlog_ms(),
                  (unsigned long)idx);
  else
    d3d8_lightlog("SETLIGHT t=%lu idx=%lu type=%u "
                  "diffuse=%.4f,%.4f,%.4f,%.4f specular=%.4f,%.4f,%.4f,%.4f "
                  "ambient=%.4f,%.4f,%.4f,%.4f pos=%.2f,%.2f,%.2f "
                  "dir=%.3f,%.3f,%.3f range=%.2f falloff=%.2f "
                  "atten=%.6f,%.6f,%.8f theta=%.3f phi=%.3f",
                  d3d8_lightlog_ms(), (unsigned long)idx,
                  ((const uint32_t *)l)[0], l[1], l[2], l[3], l[4], l[5], l[6],
                  l[7], l[8], l[9], l[10], l[11], l[12], l[13], l[14], l[15],
                  l[16], l[17], l[18], l[19], l[20], l[21], l[22], l[23], l[24],
                  l[25]);
  if (idx >= D3D8_MAX_LIGHTS)
    light_index_refused("SetLight", idx);
  if (!l || idx >= D3D8_MAX_LIGHTS) {
    d3d8_ret(C, D3DERR_INVALIDCALL);
    return;
  }
  /*
   * X2_LIGHT_RAW=<n> -- the D3DLIGHT8 as WORDS, before any interpretation.
   *
   * A level light came back with range 1.8446743e19 (about 2^64), which is
   * not a range any engine sets; that is what a misread field looks like.
   * The only way to settle a struct layout is to print the words and see
   * where the recognisable ones fall: Position and Direction are the giveaway
   * because they must be plausible world coordinates and a unit vector.
   * Type is printed as an integer because it IS one -- D3DLIGHT8 begins with
   * a DWORD, and a float view of 1 or 3 prints as 0.000 and looks like a
   * black colour channel.
   */
  {
    static long want = -2, done;
    if (want == -2) {
      const char *e = getenv("X2_LIGHT_RAW");
      want = (e && *e) ? atol(e) : -1;
    }
    if (want > 0 && done < want) {
      int k;
      done++;
      fprintf(stderr,
              "d3d8 SetLight[%u] raw %ld/%ld: type=%u then floats:", idx, done,
              want, ((const uint32_t *)l)[0]);
      for (k = 1; k < 26; k++)
        fprintf(stderr, "%s[%d]%.4g", (k % 8 == 1) ? "\n    " : " ", k,
                (double)l[k]);
      fprintf(stderr, "\n");
    }
  }
  /*
   * WHO SETS A BLACK LIGHT.
   *
   * The red chamber arrives with four point lights that have real positions,
   * real quadratic attenuation and a diffuse of exactly zero, which is not
   * something a level author places -- so the colour is lost UPSTREAM of
   * here and the question is which engine function hands it over. The word
   * at ESP is the guest return address (every emitted call site pushes one),
   * so grouping by it names the caller.
   *
   * Kept as a histogram rather than a line per call: this is called several
   * times a frame for the life of the run. Reported at exit ALWAYS, at zero
   * and with its denominator, so "no black light was ever set" and "the
   * counter never ran" cannot look the same.
   */
  {
    uint32_t ra = RD32(C->esp);
    int black = l[1] == 0.0f && l[2] == 0.0f && l[3] == 0.0f;
    int i;
    g_setlight_calls++;
    if (black)
      g_setlight_black++;
    for (i = 0; i < g_nsetlight_site; i++)
      if (g_setlight_site[i].ra == ra)
        break;
    if (i == g_nsetlight_site && i < SETLIGHT_SITES)
      g_setlight_site[g_nsetlight_site++].ra = ra;
    if (i < SETLIGHT_SITES) {
      g_setlight_site[i].calls++;
      if (black)
        g_setlight_site[i].black++;
    } else {
      g_setlight_over++;
    }
  }
  /*
   * PER INDEX, because the draw reads one index at a time.
   *
   * The run-wide total says 159 of 153,907 SetLights were black, and a draw
   * that finds four black point lights is not explained by that: what
   * matters is what the LAST call to each index left behind. Counting per
   * index turns "black lights come from somewhere" into "index 5 was set
   * 2,000 times and the last one was black".
   */
  /*
   * DID THE ENGINE'S OWN TRANSFORM RUN?
   *
   * libIGGfx setLightPosition (0x1003d5e0) does not store the position it is
   * given. It multiplies it by the top of a matrix stack and stores the
   * RESULT at D3DLIGHT8+0x34, keeping the untransformed vector 0x34 bytes
   * further on in the same record. Those two being EQUAL means the transform
   * was a no-op -- an identity or empty stack -- which would leave every
   * light in the wrong space while the geometry's world matrix is in
   * another, and that is exactly the shape of "everything lit is black".
   *
   * The record is the engine's, not ours, so the tail is validated as guest
   * memory in its own right before it is read; a light whose tail is not
   * addressable is counted rather than assumed either way.
   */
  if (idx < D3D8_MAX_LIGHTS) {
    const float *tail =
        (const float *)d3d8_guest_ptr(d3d8_arg(C, 1) + 0x68u, NULL);
    if (!tail) {
      g_light_tail_unreadable++;
    } else if (l[13] == tail[0] && l[14] == tail[1] && l[15] == tail[2]) {
      g_light_untransformed++;
    } else {
      g_light_transformed++;
    }
  }
  if (idx < D3D8_MAX_LIGHTS) {
    g_light_slot[idx].calls++;
    if (l[1] == 0.0f && l[2] == 0.0f && l[3] == 0.0f)
      g_light_slot[idx].black++;
    g_light_slot[idx].last[0] = l[1];
    g_light_slot[idx].last[1] = l[2];
    g_light_slot[idx].last[2] = l[3];
    g_light_slot[idx].last_type = ((const uint32_t *)l)[0];
  }
  /*
   * X2_LIGHT_ADDR=1 -- the GUEST ADDRESS the engine handed this light at.
   *
   * Ground truth for tools/light_probe.py. A matcher that finds D3DLIGHT8
   * records by shape has to be checked against a case it MUST find, in real
   * program memory rather than in random bytes: validated against random
   * bytes alone it had zero false positives and then matched six million
   * times in a live process, because real memory is full of 0.0 and 1.0
   * floats. These addresses are the positive control.
   */
  {
    static int addr_on = -1, told;
    if (addr_on < 0) {
      const char *e = getenv("X2_LIGHT_ADDR");
      addr_on = (e && *e) ? atoi(e) : 0;
    }
    if (addr_on && told < 24) {
      told++;
      fprintf(stderr,
              "[LIGHT ADDR] index %u at guest 0x%08x, type %u, "
              "diffuse %.4f %.4f %.4f\n",
              idx, d3d8_arg(C, 1), ((const uint32_t *)l)[0], l[1], l[2], l[3]);
    }
  }
  memcpy(d3d8_device_state()->light[idx], l,
         sizeof d3d8_device_state()->light[0]);
  d3d8_device_state()->light_set[idx] = 1;
  d3d8_ret(C, D3D_OK);
}

/*
 * What the most recent SetLight left in this slot.
 *
 * The draw path reads the light table that SetLight writes, so the two cannot
 * disagree -- unless something between them changes it. A gameplay frame shows
 * four enabled point lights with a diffuse of 0.00 while the engine's own
 * calls for those indices carry real colours, and only a comparison AT DRAW
 * TIME can tell "the engine blacked them just now" from "we lost the colour".
 * Returns 0 when the slot has never been set, which is a third answer and not
 * the same as either.
 */
int d3d8_last_setlight_diffuse(unsigned idx, float out[3]) {
  if (idx >= D3D8_MAX_LIGHTS || !g_light_slot[idx].calls)
    return 0;
  out[0] = g_light_slot[idx].last[0];
  out[1] = g_light_slot[idx].last[1];
  out[2] = g_light_slot[idx].last[2];
  return 1;
}

void d3d8_setlight_report(void) {
  int i;
  printf("  d3d8 SetLight: %lu call(s), %lu of them with a BLACK diffuse, "
         "from %d distinct call site(s)%s\n",
         g_setlight_calls, g_setlight_black, g_nsetlight_site,
         g_setlight_over ? " (the site table is FULL -- some are not listed)"
                         : "");
  /* AT ZERO and with its denominator: a line that appears only when
     something is wrong cannot be told from a check that never ran. */
  printf("         %lu of %lu SetLight/LightEnable call(s) named an index "
         "this device cannot hold (capacity %d, highest asked for %lu)\n",
         g_light_idx_refused, g_setlight_calls + g_lightenable_calls,
         D3D8_MAX_LIGHTS, g_light_idx_refused ? g_light_idx_max_seen : 0UL);
  if (!g_setlight_calls) {
    printf("         SetLight was never called, so this run says nothing "
           "about where light colour comes from.\n");
    return;
  }
  printf("         the engine transforms a light POSITION by the top of its "
         "own matrix stack before handing it over (libIGGfx 0x1003d5e0) and "
         "keeps the untransformed vector beside it:\n"
         "         %lu call(s) arrived TRANSFORMED, %lu arrived with the two "
         "equal (the transform was a no-op), %lu had no readable record "
         "tail.\n",
         g_light_transformed, g_light_untransformed, g_light_tail_unreadable);
  for (i = 0; i < (int)D3D8_MAX_LIGHTS; i++) {
    if (!g_light_slot[i].calls)
      continue;
    printf("         light[%d] %lu call(s), %lu black; LAST was type %u "
           "diffuse %.3f %.3f %.3f%s\n",
           i, g_light_slot[i].calls, g_light_slot[i].black,
           g_light_slot[i].last_type, g_light_slot[i].last[0],
           g_light_slot[i].last[1], g_light_slot[i].last[2],
           (g_light_slot[i].last[0] == 0.0f &&
            g_light_slot[i].last[1] == 0.0f && g_light_slot[i].last[2] == 0.0f)
               ? "   <- BLACK"
               : "");
  }
  for (i = 0; i < g_nsetlight_site; i++) {
    uint32_t ra = g_setlight_site[i].ra;
    const char *nm = x86_native_name_at(ra);
    X86Module *rm = x86_module_for(ra);
    printf("         0x%08x  %lu call(s), %lu black", ra,
           g_setlight_site[i].calls, g_setlight_site[i].black);
    if (nm)
      printf("  -- %s\n", nm);
    else if (rm)
      printf("  -- inside %s at guest 0x%08x, not at a named body\n", rm->name,
             rm->preferred + (ra - *rm->base));
    else
      printf("  -- in NO module; the return address is not trustworthy\n");
  }
}

void d3d8_dev_LightEnable(D3D8Object *self, CPU *C) {
  uint32_t idx = d3d8_arg(C, 0), on = d3d8_arg(C, 1);
  (void)self;
  /* Logged BEFORE the range refusal: an index we refuse and the control
     accepts is exactly the kind of difference this log exists to show, and
     it cannot show it from behind the refusal. */
  d3d8_lightlog("LIGHTENABLE t=%lu idx=%lu on=%d", d3d8_lightlog_ms(),
                (unsigned long)idx, on != 0);
  g_lightenable_calls++;
  if (idx >= D3D8_MAX_LIGHTS) {
    light_index_refused("LightEnable", idx);
    d3d8_ret(C, D3DERR_INVALIDCALL);
    return;
  }
  d3d8_device_state()->light_on[idx] = on != 0;
  d3d8_ret(C, D3D_OK);
}
