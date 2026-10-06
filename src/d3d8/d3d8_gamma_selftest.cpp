/* SetGammaRamp and GetGammaRamp through the production device vtable. */
#include "d3d8_gamma_selftest.h"
#include "../native/x2_log.h"

#include "d3d8_com.h"
#include "d3d8_device.h"
#include "d3d8_selftest_call.h"
#include "d3d8_types.h"
#include "guest_heap.h"
#include "guest_memory.h"
#include "x86rt.h"

#include <stdint.h>
#include <string.h>

/*
 * The gamma ramp, driven through the real device vtable.
 *
 * The engine calls SetGammaRamp(0, <ramp>) during renderer init -- measured,
 * from the unimplemented-method report. A D3DGAMMARAMP is three arrays of 256
 * 16-bit entries, one per channel.
 *
 * This backend cannot programme a hardware gamma ramp: it presents through a
 * Vulkan swapchain and there is no ramp to set. So what matters is not that
 * the call returns, but that the layer knows and SAYS whether the ramp it was
 * given would have changed anything -- an IDENTITY ramp costs nothing to
 * ignore, a curved one is a visible difference from the original game. The
 * test therefore checks the round trip AND the identity verdict, in both
 * directions: a discriminator only trusted after it has been run against both
 * classes has been run against neither.
 */
static void write_ramp(uint32_t base, int curved) {
  int ch, i;
  for (ch = 0; ch < 3; ch++)
    for (i = 0; i < 256; i++) {
      unsigned v = (unsigned)i * 257u; /* the identity ramp */
      if (curved && i == 128)
        v = 0u;
      WR16(base + (uint32_t)(ch * 256 + i) * 2u, (uint16_t)v);
    }
}

int d3d8_gamma_selftest(void) {
  D3D8Object *dev;
  uint32_t args[2], ramp, back;
  int fails = 0, i;

  x2_log_info("\n=== d3d8 gamma selftest: through the device vtable ===\n");
  d3d8_device_install();
  dev = d3d8_object_new(D3D8_IF_IDirect3DDevice8, NULL);
  ramp = guest_malloc(3 * 256 * 2);
  back = guest_malloc(3 * 256 * 2);

  write_ramp(ramp, 0);
  args[0] = 0;
  args[1] = ramp;
  d3d8_selftest_call(dev, 18, args, 2); /* SetGammaRamp */
  if (d3d8_device_gamma_curved()) {
    x2_log_info("d3d8 gamma selftest: FAILED -- an IDENTITY ramp was reported "
                "as curved, so the warning fires on every run and means "
                "nothing.\n");
    fails++;
  }

  memset(guest_memory_pointer(back), 0xA5, 3 * 256 * 2);
  args[0] = back;
  d3d8_selftest_call(dev, 19, args, 1); /* GetGammaRamp */
  for (i = 0; i < 3 * 256; i++)
    if (RD16(back + (uint32_t)i * 2u) != RD16(ramp + (uint32_t)i * 2u)) {
      x2_log_info("d3d8 gamma selftest: FAILED -- entry %d came back as "
                  "0x%04x, not 0x%04x.\n",
                  i, RD16(back + (uint32_t)i * 2u),
                  RD16(ramp + (uint32_t)i * 2u));
      fails++;
      break;
    }

  write_ramp(ramp, 1); /* one entry bent */
  args[0] = 0;
  args[1] = ramp;
  d3d8_selftest_call(dev, 18, args, 2);
  if (!d3d8_device_gamma_curved()) {
    x2_log_info("d3d8 gamma selftest: FAILED -- a ramp with a bent entry was "
                "called identity. Then a game that darkens the screen through "
                "gamma would do it silently and nothing would say so.\n");
    fails++;
  }

  args[0] = 0;
  args[1] = 0;
  d3d8_selftest_call(dev, 18, args, 2); /* a NULL ramp */
  x2_log_info(
      "d3d8 gamma selftest: %s\n",
      fails ? "FAILED"
            : "PASSED -- the ramp round-trips, and identity is told apart from "
              "curved in both directions");
  return fails;
}
