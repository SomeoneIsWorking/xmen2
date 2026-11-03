#include "display_settings_document.hpp"

#include "hud_settings_document.hpp"
#include "rml_text.hpp"

#include <RmlUi/Core.h>

#include <sstream>

#include "display_geometry.h"
#include "live_resolution.h"
#include "resolution_ladder.h"

namespace x2::ui {
namespace {

const char *const kWiredControls[] = {"resolution", "window-mode",
                                      "dynamic-shadows", "shadow-resolution"};

} // namespace

std::string
display_settings_document_rml(const x2::config::Settings &settings) {
  std::ostringstream rml;
  char resolution_label[16];

  unsigned rendered_w, rendered_h;

  x2::presentation::resolution_label(settings.render_lines, resolution_label,
                                     sizeof resolution_label);
  x2::presentation::live_resolution_rendered(&settings, &rendered_w,
                                             &rendered_h);
  /* The size beside the choice is the one the game renders at, the number
     the player will recognise in a screenshot or a bug report. */
  rml << "<pane><div class='section-heading'>Display</div>"
      << "<select-button id='resolution'><key>Resolution</key><value>"
      << resolution_label << " (" << rendered_w << "x" << rendered_h
      << ")</value></select-button>";
  /* Where the host's one window is the display there is no window mode. */
  if (!x2::presentation::host_window_fills_display())
    rml << "<select-button id='window-mode'><key>Window mode</key><value>"
        << escape_rml(window_mode_name(settings.window_mode))
        << "</value></select-button>";
  rml << "<select-button id='dynamic-shadows'><key>Dynamic "
         "shadows</key><value>"
      << (settings.dynamic_shadows ? "On" : "Off") << "</value></select-button>"
      << "<select-button id='shadow-resolution'><key>Shadow "
         "quality</key><value>"
      << settings.shadow_resolution << "</value></select-button>"
      << "<div class='help'>Native renders at the screen's own pixel size; "
         "in a window it is the window's size. A preset is a height whose "
         "width follows the screen's shape. Exclusive fullscreen switches "
         "the display to the resolved size.</div>"
      << "<p id='status' class='status'></p><spacer></spacer></pane>"
      << hud_settings_document_rml(settings.hud);
  return rml.str();
}

void display_settings_document_wire(Rml::ElementDocument &document,
                                    Rml::EventListener &listener) {
  for (const char *id : kWiredControls)
    if (Rml::Element *element = document.GetElementById(id)) {
      element->AddEventListener("click", &listener);
      element->AddEventListener("keydown", &listener);
    }
  hud_settings_document_wire(document, listener);
}

} // namespace x2::ui
