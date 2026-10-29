#include "control_performance_route.h"

#include "control.h"
#include "control_command_bridge.h"
#include "control_http.h"
#include "host_sampler.hpp"
#include "x86_hotep.h"

#include <stdlib.h>
#include <string.h>

#include <string>

namespace x2::native {

void control_performance_reset_route(x2::native::Socket fd) {
  char reason[192];
  const int result = control_command_performance_reset(reason, sizeof reason);
  if (result < 0) {
    control_reply_text(fd, 504, "Gateway Timeout",
                       "the guest did not reach an input poll within 10s; "
                       "the frame-time window was NOT reset.\n");
  } else if (!result) {
    control_reply_text(fd, 409, "Conflict", "%s\n", reason);
  } else {
    control_reply_text(fd, 200, "OK", "%s\n", reason);
  }
}

/*
 * Arm (or disarm) the hot-guest-entry-point probe while the game runs.
 *
 * X2_HOTEP answers where an unattributed frame goes, but a released build has
 * no environment to set it in: the only way to measure a phone was to install
 * a debug build, which changes the signing identity and destroys the player's
 * imported game. The probe's arming is a single store the dispatch path reads,
 * so it is safe to flip from here, and the heartbeat prints the split on its
 * next interval. n=0 disarms and the probe goes back to answering nothing.
 */
void control_performance_probe_route(x2::native::Socket fd, const char *query) {
  const char *n = strstr(query, "n=");
  unsigned long want;

  if (!n) {
    control_reply_text(fd, 400, "Bad Request",
                       "give the number of entry points to track: "
                       "/performance/probe?n=4096 (n=0 disarms)\n");
    return;
  }
  want = strtoul(n + 2, NULL, 10);
  x86_hotep_arm(n + 2);
  control_reply_text(fd, 200, "OK",
                     want ? "hot-entry-point probe armed for %lu entry "
                            "point(s); the next heartbeat prints the "
                            "wall-time split.\n"
                          : "hot-entry-point probe disarmed (%lu).\n",
                     want);
}

} // namespace x2::native

namespace x2::native {

namespace {

struct SampleRequest {
  unsigned hz;
  std::size_t count;
  bool arm;
  bool ok;
  std::string text;
};

/* Runs at the guest input poll, on the thread that runs the game's frame. */
void sample_on_guest_thread(void *context) {
  auto *request = static_cast<SampleRequest *>(context);
  HostSampler &sampler = host_sampler();
  if (request->arm) {
    request->ok = sampler.start(request->hz, request->count, request->text);
    return;
  }
  sampler.stop();
  request->ok = true;
  request->text = sampler.report();
}

} // namespace

/*
 * /performance/sample?hz=1000&n=20000 arms a CPU-time sampler on the game's
 * frame thread; a later /performance/sample stops it and returns the loaded
 * modules and every sample's PC and link register.
 */
void control_performance_sample_route(x2::native::Socket fd,
                                      const char *query) {
  SampleRequest request{};
  const char *hz = strstr(query, "hz=");
  const char *n = strstr(query, "n=");
  request.arm = hz || n;
  if (request.arm) {
    request.hz = hz ? static_cast<unsigned>(strtoul(hz + 3, NULL, 10)) : 1000u;
    request.count =
        n ? static_cast<std::size_t>(strtoul(n + 2, NULL, 10)) : 20000u;
  }
  if (control_command_guest_read(sample_on_guest_thread, &request) < 0) {
    control_reply_text(fd, 504, "Gateway Timeout",
                       "the guest did not reach an input poll within 10s\n");
    return;
  }
  if (!request.ok) {
    control_reply_text(fd, 409, "Conflict", "%s\n", request.text.c_str());
  } else if (request.arm) {
    control_reply_text(fd, 200, "OK",
                       "sampling the frame thread %u time(s) per CPU second "
                       "until %zu sample(s) are held\n",
                       request.hz, request.count);
  } else {
    control_reply_bytes(fd, 200, "OK", "text/plain", request.text.data(),
                        request.text.size());
  }
}

} // namespace x2::native
