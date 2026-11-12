/*
 * RmlUi/SDL_GPU lifetime for the port's overlays that are not settings:
 * touch controls, the skip button, the revive prompt and the autosave
 * notice. Each document owns its own content.
 */
#include "rmlui_ui.h"
#include "../config/environment.h"

#include <RmlUi/Core.h>
#include <RmlUi_Platform_SDL.h>
#include <RmlUi_Renderer_SDL_GPU.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <memory>

#include "aspect_fit.h"
#include "autosave_notice_document.hpp"
#include "environment.h"
#include "extraction_revive_document.hpp"
#include "igb_textures.hpp"
#include "skip_document.hpp"
#include "touch_document.hpp"
#include "touch_runtime.h"
#include "ui_resources.h"
#include "x2_log.h"

namespace x2::ui {

namespace {

std::unique_ptr<SystemInterface_SDL> system_interface;
std::unique_ptr<x2::ui::IgbTextureRenderInterface> render_interface;
Rml::Context *context;
x2::ui::SkipDocument skip_document;
x2::ui::ExtractionReviveDocument revive_document;
x2::ui::AutosaveNoticeDocument autosave_document;
bool initialized;

void sync_surface_metrics(SDL_Window *window, unsigned width, unsigned height) {
  /* The RCSS is authored in the launcher's 1280x720 default design space.
     RmlUi's dp units need the same aspect-fit scale as the retail frame or
     a resolution increase makes every label smaller relative to the screen.

     The swapchain dimensions are physical pixels when SDL exposes a
     high-density surface, so that ratio already contains the display scale.
     max() also covers backends that report logical swapchain dimensions and
     expose the density only through SDL_GetWindowDisplayScale(). */
  constexpr unsigned design_width = 1280;
  constexpr unsigned design_height = 720;
  x2::presentation::AspectRect fitted{};
  float display_scale = SDL_GetWindowDisplayScale(window);
  if (!(display_scale > 0.0f))
    display_scale = 1.0f;
  float resolution_scale = 1.0f;
  if (x2::presentation::aspect_fit(width, height, design_width, design_height,
                                   &fitted))
    resolution_scale =
        static_cast<float>(fitted.height) / static_cast<float>(design_height);
  context->SetDimensions(Rml::Vector2i((int)width, (int)height));
  context->SetDensityIndependentPixelRatio(
      std::max(display_scale, resolution_scale));
}

void discard_partial_initialization() {
  autosave_document.shutdown();
  revive_document.shutdown();
  skip_document.shutdown();
  x2::ui::touch_document_shutdown();
  context = nullptr;
  Rml::Shutdown();
  if (render_interface)
    render_interface->Shutdown();
  render_interface.reset();
  system_interface.reset();
}

/*
 * Say which step failed, once.
 *
 * ui_render calls initialize every frame, so a failure here is retried
 * forever; reporting once names the step without a per-frame flood.
 */
bool initialize_failed(const char *step) {
  static bool reported;
  if (!reported)
    x2_log_error("RMLUI: overlay unavailable, %s failed.\n", step);
  reported = true;
  return false;
}

bool initialize(SDL_GPUDevice *device, SDL_Window *window, unsigned width,
                unsigned height) {
  if (initialized)
    return true;
  system_interface = std::make_unique<SystemInterface_SDL>(window);
  render_interface =
      std::make_unique<x2::ui::IgbTextureRenderInterface>(device, window);
  Rml::SetSystemInterface(system_interface.get());
  Rml::SetRenderInterface(render_interface.get());
  if (!Rml::Initialise()) {
    render_interface.reset();
    system_interface.reset();
    return initialize_failed("Rml::Initialise");
  }
  const char *regular = x2::ui::ui_resource_path("LatoLatin-Regular.ttf");
  if (!Rml::LoadFontFace(regular))
    x2_log_error("RMLUI: could not load font %s\n", regular);
  const char *bold = x2::ui::ui_resource_path("LatoLatin-Bold.ttf");
  if (!Rml::LoadFontFace(bold))
    x2_log_error("RMLUI: could not load font %s\n", bold);
  context =
      Rml::CreateContext("x2-overlay", Rml::Vector2i((int)width, (int)height));
  if (!context) {
    discard_partial_initialization();
    return initialize_failed("Rml::CreateContext");
  }
  sync_surface_metrics(window, width, height);
  if (!x2::ui::touch_document_load(context)) {
    discard_partial_initialization();
    return initialize_failed("loading the touch overlay document");
  }
  if (!skip_document.load(context)) {
    discard_partial_initialization();
    return initialize_failed("loading the skip button document");
  }
  if (!revive_document.load(context)) {
    discard_partial_initialization();
    return initialize_failed("loading the revive prompt document");
  }
  if (!autosave_document.load(context)) {
    discard_partial_initialization();
    return initialize_failed("loading the autosave notice document");
  }
  initialized = true;
  x2_log_info("RMLUI: overlays initialized.\n");
  return true;
}

} // namespace

void ui_render(SDL_GPUDevice *device, SDL_GPUCommandBuffer *command_buffer,
               SDL_GPUTexture *swapchain, uint32_t width, uint32_t height,
               SDL_Window *window) {
  const bool touch_visible = x2::input::touch_runtime_overlay_on_screen();
  /* Its own gate: a cinematic hides the gameplay overlay. */
  const bool skip_visible = x2::ui::SkipDocument::wanted();
  /* The paid revive offer: gameplay only. */
  const bool revive_visible = x2::ui::ExtractionReviveDocument::wanted();
  const bool autosave_visible = x2::ui::AutosaveNoticeDocument::wanted();
  if ((!touch_visible && !skip_visible && !revive_visible &&
       !autosave_visible) ||
      !device || !command_buffer || !swapchain || !window ||
      !initialize(device, window, width, height)) {
    x2::input::touch_runtime_note_drawn({});
    return;
  }
  x2::ui::touch_document_set_visible(touch_visible);
  /* The overlay may have been hidden while the window moved to a display
     with another DPI. Hidden events deliberately bypass RmlUi, so reconcile
     both pixel dimensions and dp scale from current output state here. */
  sync_surface_metrics(window, width, height);
  /* Update can compile geometry and upload font textures. The SDL_GPU
     backend must already own this frame's command buffer before that work;
     doing BeginFrame after Update records uploads against stale state and
     corrupts both UI vertices and the frame under the overlay. */
  render_interface->BeginFrame(command_buffer, swapchain, width, height);
  x2::ui::touch_document_update();
  skip_document.update();
  revive_document.update();
  autosave_document.update();
  context->Update();
  context->Render();
  render_interface->EndFrame();
  x2::input::touch_runtime_note_drawn(x2::ui::touch_document_drawn());
}

void ui_gpu_shutdown(void) {
  if (!initialized)
    return;
  autosave_document.shutdown();
  revive_document.shutdown();
  skip_document.shutdown();
  x2::ui::touch_document_shutdown();
  context = nullptr;
  Rml::Shutdown();
  render_interface->Shutdown();
  render_interface.reset();
  system_interface.reset();
  initialized = false;
}

IgbTextureRenderInterface *igb_texture_interface() {
  return render_interface.get();
}

} // namespace x2::ui
