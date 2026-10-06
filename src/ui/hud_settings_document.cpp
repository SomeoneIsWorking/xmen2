#include "hud_settings_document.hpp"

#include <RmlUi/Core.h>

#include <sstream>

namespace x2::ui {
namespace {

struct PercentControl {
  const char *id;
  const char *label;
  unsigned X2HudSettings::*field;
  unsigned minimum;
  unsigned maximum;
  unsigned step;
};

constexpr PercentControl controls[] = {
    {"hud-vitals", "Health and energy size",
     &X2HudSettings::vitals_scale_percent, X2_HUD_SCALE_MIN, X2_HUD_SCALE_MAX,
     25},
    {"hud-potions", "Potion size", &X2HudSettings::potions_scale_percent,
     X2_HUD_SCALE_MIN, X2_HUD_SCALE_MAX, 25},
    {"hud-portraits", "Portrait size", &X2HudSettings::portraits_scale_percent,
     X2_HUD_SCALE_MIN, X2_HUD_SCALE_MAX, 25},
    {"hud-inset", "Screen edge spacing", &X2HudSettings::safe_inset_percent, 0,
     X2_HUD_INSET_MAX, 2}};

void wire(Rml::ElementDocument &document, Rml::EventListener &listener,
          const char *id) {
  if (auto *element = document.GetElementById(id)) {
    element->AddEventListener("click", &listener);
    element->AddEventListener("keydown", &listener);
  }
}

} // namespace

std::string hud_settings_document_rml(const X2HudSettings &settings) {
  std::ostringstream rml;
  rml << "<pane><div class='section-heading'>HUD</div>"
         "<select-button id='hud-layout'><key>Layout</key><value>"
      << x2_hud_layout_label(settings.layout) << "</value></select-button>";
  for (const auto &control : controls)
    rml << "<select-button id='" << control.id << "'><key>" << control.label
        << "</key><value>" << settings.*(control.field)
        << "%</value></select-button>";
  rml << "<div class='help'>Automatic uses the mobile layout while touch "
         "controls are active. Mobile moves the HUD above the controls. "
         "Size and edge spacing apply to the mobile layout.</div>"
         "<spacer></spacer></pane>";
  return rml.str();
}

void hud_settings_document_wire(Rml::ElementDocument &document,
                                Rml::EventListener &listener) {
  wire(document, listener, "hud-layout");
  for (const auto &control : controls)
    wire(document, listener, control.id);
}

bool hud_settings_document_change(X2HudSettings &settings,
                                  const std::string &id) {
  if (id == "hud-layout") {
    settings.layout = static_cast<X2HudLayout>(
        (static_cast<unsigned>(settings.layout) + 1u) % 3u);
    return true;
  }
  for (const auto &control : controls) {
    if (id != control.id)
      continue;
    unsigned &value = settings.*(control.field);
    unsigned next =
        control.minimum +
        ((value - control.minimum) / control.step + 1) * control.step;
    value = next > control.maximum ? control.minimum : next;
    return true;
  }
  return false;
}

} // namespace x2::ui
