/* The every-ending report roll-call.
 *
 * Nothing in this program stops on its own: every run ends in a timeout, a
 * frame limit or a kill, and the shutdown report is written from a path that
 * can be cut short. So every instrument's report is called HERE, on every
 * ending, at zero and with its denominator -- the alternative was atexit,
 * which the clean _exit stops skip, and which is how successful runs used to
 * lose the very numbers that judge whether a change helped.
 *
 * Extracted from x2native.c: the roll-call is a distinct concern from launch
 * and the run loop, and x2native.c had crossed its frozen structure limit.
 * The `killed` flag decides only whether the boundary ring dumps -- resolving
 * a name per ring entry against 16k functions took minutes, long enough that
 * the timeout killed the process during its own clean shutdown.
 */
#include "heartbeat.h"
#include "boot_blackout.h"
#include "dialog_selection_scale.h"
#include "x86_engine.h"
#include "input_record.h"
#include "live_session.h"
#include "prompt_glyph_draw.h"
#include "prompt_glyph_batch.h"
#include "prompt_glyph_metrics.h"
#include "prompt_glyph_quads.h"
#include "gpu_prompt_glyphs.h"
#include "ui_transform.h"
#include "threads.h"
#include "touch_hud_runtime.h"
#include "x86_reached.h"
#include "x86rt_native.h"

#include <stdio.h>

extern void x2_texture_probe_report(void);

void x2_interrupt_reports(int killed)
{
    extern void d3d8_host_report(void);
    extern void guest_heap_report(void);
    extern void guest_thread_report(void);
    extern void k32_critsec_report(void);
    extern void dinput_device_report(void);
    extern void dinput_pad_report(void), pad_glyphs_report(void);
    extern void dialog_prompts_report(void);
    extern void x2_ui_text_scale_report(void);
    /* The reached set is collected on EVERY run that armed it, so its report
       belongs here too: x86_diag_dump runs only on the abort paths, and a
       clean X2_MAX_FRAMES stop used to throw the collection away. The ring
       stays kill-only -- resolving a name per entry took minutes. */
    x86_reached_report();
    x2_texture_probe_report();
    x2_prompt_draw_report();
    x2_prompt_glyph_metrics_report();
    x2_prompt_quads_report();
    x2_prompt_glyph_batch_report();
    x2_ui_transform_report();
    gpu_prompt_glyphs_report();
    {
        char blackout[256];
        x2_boot_blackout_report(blackout, sizeof blackout);
        printf("        %s", blackout);
    }
    x2_engine_report();
    d3d8_host_report();
    guest_heap_report();
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
    x2_ui_text_scale_report();
    x2_dialog_selection_scale_report();
    x2_touch_hud_report();
    dinput_device_report();
    dinput_pad_report();
    input_record_report();
    live_session_stop();
    pad_glyphs_report();
    dialog_prompts_report();
    { extern void dsound_report(void), x2_movie_report(void); dsound_report(); x2_movie_report(); }
    { extern void k32_asset_report(void), ws2_report(void);
      k32_asset_report(); ws2_report(); }
    { extern void conversation_report(void); conversation_report(); }
    { extern void script_trace_report(void); script_trace_report(); }
    { extern void x86_profiler_report(void); x86_profiler_report(); }
    /* shell32's save-path report was registered with atexit, and the clean
       X2_MAX_FRAMES stop leaves through _exit -- so on precisely the runs
       that reach gameplay it had never printed once. Same defect the input
       reports had; same fix. */
    { extern void shell32_report(void); shell32_report(); }
        { extern void d3d8_vsconst_caller_report(void);
      d3d8_vsconst_caller_report(); }
    x86_epcount_report();
    fflush(stdout);
    if (killed)
        x86_diag_dump();
    else
        printf("  (the boundary ring is not dumped: this run stopped because "
               "it reached X2_MAX_FRAMES, so there is no spin to locate.)\n");
    fflush(stdout);
}
