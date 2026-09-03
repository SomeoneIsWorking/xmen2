/*
 * Reconcile the port's configured output mode at the retail settings-load
 * boundary. A fresh profile takes XMen2.exe 0x00619770's default branch and
 * persists 800x600 after the host's early publication; a warm profile reads
 * the published value. Preserve that complete body so every other first-run
 * default and Version=7 write remains retail-owned, then republish and invoke
 * the retail Resolution reader itself before device creation.
 */
#include "display_mode_runtime.h"
#include "display_mode_seed.h"
#include "guest_memory.h"
#include "settings_store.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  LINKED_SETTINGS_LOAD = 0x00619770u,
  EXE_PREFERRED_BASE = 0x00400000u,
  RVA_BUILD_REGISTRY_CONTEXT = 0x00216a70u,
  RVA_READ_STRING = 0x00216e10u,
  RVA_PUBLISHER_NAME = 0x002a3a64u,
  RVA_PRODUCT_NAME = 0x002a3a70u,
  RVA_DEMO_PRODUCT_NAME = 0x002a3a80u,
  RVA_RESOLUTION_PATH = 0x002a4dccu,
  RVA_RESOLUTION_DEFAULT = 0x002a4e40u,
  RVA_RESOLUTION_OUTPUT = 0x00668d9cu,
  RVA_DEMO_FLAG = 0x002f3c2du,
  RVA_TITLE_WIDTH = 0x00609ffcu,
  RVA_TITLE_HEIGHT = 0x0060a000u,
  RVA_DISPLAY_SINGLETON = 0x0060a138u,
  RVA_DISPLAY_VTABLE = 0x002a3a9cu,
  REGISTRY_CONTEXT_BYTES = 0x78u,
  RESOLUTION_CAPACITY = 10u
};

enum {
  DISPLAY_ASPECT = 0x10u,
  DISPLAY_LAYOUT_ASPECT = 0x14u,
  DISPLAY_WIDTH = 0x20u,
  DISPLAY_HEIGHT = 0x24u,
  DISPLAY_X_PER_PIXEL = 0x40u,
  DISPLAY_Y_PER_PIXEL = 0x44u,
  DISPLAY_OBJECT_BYTES = 0x64u
};

static void refuse(const char *reason, const char *expected,
                   const char *actual) {
  fprintf(stderr, "DISPLAY RUNTIME: REFUSED: %s; expected '%s', got '%s'.\n",
          reason, expected ? expected : "?", actual ? actual : "?");
  fflush(stderr);
  abort();
}

static uint32_t executable_base(void) {
  X86Module *module;

  for (module = x86_modules(); module; module = module->next) {
    if (!strcmp(module->name, "XMen2.exe")) {
      if (!module->base || !*module->base ||
          module->preferred != EXE_PREFERRED_BASE)
        return 0u;
      return *module->base;
    }
  }
  return 0u;
}

static int report_failure(char *why, int whyn, const char *message) {
  if (why && whyn > 0)
    snprintf(why, (size_t)whyn, "%s", message);
  return 0;
}

static float read_float(uint32_t address) {
  float value;
  memcpy(&value, guest_memory_const_pointer(address), sizeof value);
  return value;
}

static void write_float(uint32_t address, float value) {
  memcpy(guest_memory_pointer(address), &value, sizeof value);
}

int x2_display_mode_runtime_apply(uint32_t width, uint32_t height, char *why,
                                  int whyn) {
  uint32_t exe = executable_base();
  uint32_t display, old_width, old_height;
  float old_aspect, old_layout_aspect, x_per_pixel, y_per_pixel;
  float height_scale, aspect;

  if (!width || !height)
    return report_failure(why, whyn,
                          "title display dimensions must be non-zero");
  if (!exe)
    return report_failure(why, whyn, "XMen2.exe mapping is unavailable");
  if (!guest_memory_is_readable(exe + RVA_TITLE_WIDTH, 8u))
    return report_failure(why, whyn, "title display globals are unavailable");

  display = exe + RVA_DISPLAY_SINGLETON;
  if (!guest_memory_is_readable(display, DISPLAY_OBJECT_BYTES))
    return report_failure(why, whyn, "title display singleton is unavailable");
  if (RD32(display) != exe + RVA_DISPLAY_VTABLE)
    return report_failure(why, whyn,
                          "title display singleton has an unexpected type");

  old_width = RD32(exe + RVA_TITLE_WIDTH);
  old_height = RD32(exe + RVA_TITLE_HEIGHT);
  if (!old_width || !old_height || RD32(display + DISPLAY_WIDTH) != old_width ||
      RD32(display + DISPLAY_HEIGHT) != old_height)
    return report_failure(why, whyn, "title display dimensions disagree");

  old_aspect = read_float(display + DISPLAY_ASPECT);
  old_layout_aspect = read_float(display + DISPLAY_LAYOUT_ASPECT);
  x_per_pixel = read_float(display + DISPLAY_X_PER_PIXEL);
  y_per_pixel = read_float(display + DISPLAY_Y_PER_PIXEL);
  if (!isfinite(old_aspect) || old_aspect <= 0.0f ||
      !isfinite(old_layout_aspect) || old_layout_aspect <= 0.0f ||
      !isfinite(x_per_pixel) || x_per_pixel <= 0.0f || !isfinite(y_per_pixel) ||
      y_per_pixel <= 0.0f)
    return report_failure(why, whyn,
                          "title display aspect or pixel scales are invalid");

  aspect = (float)width / (float)height;
  height_scale = (float)old_height / (float)height;
  WR32(exe + RVA_TITLE_WIDTH, width);
  WR32(exe + RVA_TITLE_HEIGHT, height);
  WR32(display + DISPLAY_WIDTH, width);
  WR32(display + DISPLAY_HEIGHT, height);
  write_float(display + DISPLAY_ASPECT, aspect);
  write_float(display + DISPLAY_LAYOUT_ASPECT,
              old_layout_aspect / old_aspect * aspect);
  write_float(display + DISPLAY_X_PER_PIXEL, x_per_pixel * height_scale);
  write_float(display + DISPLAY_Y_PER_PIXEL, y_per_pixel * height_scale);

  if (why && whyn > 0)
    snprintf(why, (size_t)whyn,
             "title display state is now %ux%u (aspect %.6g)", width, height,
             (double)aspect);
  return 1;
}

static uint32_t build_registry_context(const CPU *source, uint32_t exe) {
  CPU call = *source;
  uint32_t context = call.esp - REGISTRY_CONTEXT_BYTES;
  uint32_t product = RD8(exe + RVA_DEMO_FLAG) ? exe + RVA_DEMO_PRODUCT_NAME
                                              : exe + RVA_PRODUCT_NAME;

  memset(guest_memory_pointer(context), 0, REGISTRY_CONTEXT_BYTES);
  call.esp = context;
  call.esp -= 4u;
  WR32(call.esp, product);
  call.esp -= 4u;
  WR32(call.esp, exe + RVA_PUBLISHER_NAME);
  call.ecx = context;
  x86_guest_call_args(&call, exe + RVA_BUILD_REGISTRY_CONTEXT, 8u);
  if (call.eax != context)
    refuse("retail registry context constructor returned another object", NULL,
           NULL);
  return context;
}

static void reread_resolution(const CPU *source, uint32_t exe,
                              const char *expected) {
  CPU call = *source;
  const char *actual;
  uint32_t context = build_registry_context(source, exe);

  /* Keep the reader's 0x314-byte local frame below the context, matching
     the original loader where the context lives in its caller's frame. */
  call.esp = context;
  call.esp -= 4u;
  WR32(call.esp, RESOLUTION_CAPACITY);
  call.esp -= 4u;
  WR32(call.esp, exe + RVA_RESOLUTION_OUTPUT);
  call.esp -= 4u;
  WR32(call.esp, exe + RVA_RESOLUTION_DEFAULT);
  call.esp -= 4u;
  WR32(call.esp, exe + RVA_RESOLUTION_PATH);
  call.ecx = context;
  x86_guest_call_args(&call, exe + RVA_READ_STRING, 16u);

  actual = guest_memory_const_pointer(exe + RVA_RESOLUTION_OUTPUT);
  if (!actual || strcmp(actual, expected) != 0)
    refuse("retail Resolution reader did not produce the configured mode",
           expected, actual);
}

static void x2_override_display_settings_load(CPU *C) {
  const X2Settings *settings;
  uint32_t exe;
  char expected[32];

  x86_guest_body(C, "XMen2.exe", 0x00619770u);

  settings = x2_settings_store();
  if (!x2_display_mode_seed_format(settings->width, settings->height, expected,
                                   (int)sizeof expected) ||
      strlen(expected) >= RESOLUTION_CAPACITY)
    refuse("configured mode exceeds the retail string capacity", expected,
           NULL);

  (void)x2_display_mode_seed_publish();
  if (!x2_display_mode_seed_is_current())
    refuse("configured mode could not be republished after first-run defaults",
           expected, NULL);

  exe = executable_base();
  if (!exe)
    refuse("XMen2.exe mapping is unavailable", expected, NULL);
  reread_resolution(C, exe, expected);
  fprintf(stderr,
          "DISPLAY RUNTIME: retail settings resolved configured "
          "mode %s after first-run/default initialization.\n",
          expected);
}

__attribute__((constructor)) static void
x2_display_mode_runtime_register_override(void) {
  x86_register_override("XMen2.exe", LINKED_SETTINGS_LOAD,
                        x2_override_display_settings_load);
}
