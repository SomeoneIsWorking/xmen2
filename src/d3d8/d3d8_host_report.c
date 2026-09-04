#include "../native/x2_log.h"
#include "d3d8_com.h"
#include "d3d8_device.h"
#include "d3d8_host.h"

void d3d8_host_report(void) {
  if (!d3d8_host_enabled()) {
    x2_log_info("\nd3d8: the host Direct3D 8 was NOT enabled this run, so "
                "nothing here was exercised.\n");
    return;
  }
  x2_log_info("\n=== host Direct3D 8 ===\n");
  d3d8_device_report();
  d3d8_setlight_report();
  d3d8_permissive_report();
}
