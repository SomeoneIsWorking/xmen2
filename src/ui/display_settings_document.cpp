#include "display_settings_document.hpp"

#include "hud_settings_document.hpp"
#include "rml_text.hpp"

#include <RmlUi/Core.h>

#include <sstream>

extern "C" {
#include "resolution_ladder.h"
}

namespace x2::ui {
namespace {

const char *const kWiredControls[] = {"resolution", "window-mode",
                                      "dynamic-shadows", "shadow-resolution"};

} // namespace

std::string display_settings_document_rml(const X2Settings &settings) {
  std::ostringstream rml;
  char resolution_label[16];

  x2_resolution_label(settings.height, resolution_label,
                      sizeof resolution_label);
  /* The preset is the height; the width beside it is what the display's own
     aspect ratio made of it, and is shown because it is the number the player
     will recognise in a screenshot or a bug report. */
  rml << "<pane><div class='section-heading'>Display</div>"
      << "<select-button id='resolution'><key>Resolution</key><value>"
      << resolution_label << " (" << settings.width << "x" << settings.height
      << ")</value></select-button>"
      << "<select-button id='window-mode'><key>Window mode</key><value>"
      << escape_rml(x2_window_mode_name(settings.window_mode))
      << "</value></select-button>"
      << "<select-button id='dynamic-shadows'><key>Dynamic "
         "shadows</key><value>"
      << (settings.dynamic_shadows ? "On" : "Off") << "</value></select-button>"
      << "<select-button id='shadow-resolution'><key>Shadow "
         "quality</key><value>"
      << settings.shadow_resolution << "</value></select-button>"
      << "<div class='help'>The resolution setting is a height; the width "
         "follows your display's shape. Windowed uses the selected client "
         "size. Borderless uses the desktop mode. Exclusive fullscreen "
         "switches the display to the selected resolution.</div>"
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
