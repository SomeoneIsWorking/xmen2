/* The every-ending report roll-call.
 *
 * Nothing in this program stops on its own: every run ends in a timeout, a
 * frame limit or a kill, and the shutdown report is written from a path that
 * can be cut short. So every instrument's report is called HERE, on every
 * ending, at zero and with its denominator -- the alternative was atexit,
 * which the clean _exit stops skip, and which is how successful runs used to
 * lose the very numbers that judge whether a change helped.
 *
 * Extracted from x2native.cpp: the roll-call is a distinct concern from launch
 * and the run loop, and x2native.cpp had crossed its frozen structure limit.
 * The `killed` flag decides only whether the boundary ring dumps -- resolving
 * a name per ring entry against 16k functions took minutes, long enough that
 * the timeout killed the process during its own clean shutdown.
 */
#include "../input/touch_runtime.h"
#include "alchemy_controller_bridge.h"
#include "boot_blackout.h"
#include "dialog_prompts.h"
#include "dialog_selection_scale.h"
#include "dinput_pad_report.h"
#include "gpu_prompt_glyphs.h"
#include "guest_layout_report.h"
#include "heartbeat.h"
#include "input_record.h"
#include "keycap_labels.h"
#include "live_session.h"
#include "movie.h"
#include "override_leaf.h"
#include "pad_glyphs.h"
#include "prompt_glyph_batch.h"
#include "prompt_glyph_draw.h"
#include "prompt_glyph_metrics.h"
#include "prompt_glyph_quads.h"
#include "prompt_tokens.h"
#include "script_trace.h"
#include "stick_axis_override.h"
#include "threads.h"
#include "touch_hud_runtime.h"
#include "ui_text_scale.h"
#include "ui_transform.h"
#include "x2_log.h"
#include "x86_engine.h"
#include "x86rt_native.h"

#include <stdio.h>

extern void x2_texture_probe_report(void);
extern void d3d8_host_report(void);
extern void guest_heap_report(void);
extern void guest_thread_report(void);
extern void k32_critsec_report(void);
extern void dinput_device_report(void);
extern void dsound_report(void);
extern void k32_asset_report(void), ws2_report(void);
extern void conversation_report(void);
extern void x86_profiler_report(void);
extern void shell32_report(void);
extern void d3d8_vsconst_caller_report(void);

namespace x2::native {

void interrupt_reports(int killed) {
  x2_texture_probe_report();
  x2::native::prompt_draw_report();
  x2::native::keycap_labels_report();
  x2::native::prompt_glyph_metrics_report();
  x2::native::prompt_quads_report();
  x2::native::prompt_glyph_batch_report();
  x2::native::prompt_tokens_report();
  x2::native::ui_transform_report();
  gpu_prompt_glyphs_report();
  {
    char blackout[256];
    x2::presentation::boot_blackout_report(blackout, sizeof blackout);
    x2_log_info("        %s", blackout);
  }
  engine_report();
  x86_override_leaves_report();
  d3d8_host_report();
  guest_heap_report();
  guest_layout_report();
  /* The threads and their critical sections are reported on EVERY ending,
     not only on a kill. They lived in x86_diag_dump, which the clean
     X2_MAX_FRAMES stop deliberately skips -- so the runs that WORK, the ones
     a scheduling change has to be judged on, produced no thread numbers at
     all. A counter you only see when the run failed cannot tell you the
     change helped. */
  guest_thread_report();
  guest_engine_thread_report();
  k32_critsec_report();
  /* Input reports were registered with atexit, but clean frame-limit stops
     use _exit. Print them here so successful runs retain their denominators. */
  x2::native::ui_text_scale_report();
  x2::native::dialog_selection_scale_report();
  x2::native::touch_hud_report();
  x2::input::touch_runtime_report("");
  dinput_device_report();
  dinput_pad_report();
  x2::native::stick_axis_report();
  x2::input::alchemy_controller_report();
  input_record_report();
  live_session_stop();
  pad_glyphs_report();
  dialog_prompts_report();
  {
    dsound_report();
    x2::native::movie_report();
  }
  {
    k32_asset_report();
    ws2_report();
  }
  {
    conversation_report();
  }
  {
    script_trace_report();
  }
  {
    x86_profiler_report();
  }
  /* shell32's save-path report was registered with atexit, and the clean
     X2_MAX_FRAMES stop leaves through _exit -- so on precisely the runs
     that reach gameplay it had never printed once. Same defect the input
     reports had; same fix. */
  {
    shell32_report();
  }
  {
    d3d8_vsconst_caller_report();
  }
  x86_epcount_report();
  if (killed)
    x86_diag_dump();
  else
    x2_log_info("  (the boundary ring is not dumped: this run stopped because "
                "it reached X2_MAX_FRAMES, so there is no spin to locate.)\n");
}

} // namespace x2::native
