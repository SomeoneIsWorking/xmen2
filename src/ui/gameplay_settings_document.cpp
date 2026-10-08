#include "gameplay_settings_document.hpp"

#include <RmlUi/Core.h>

#include <sstream>

namespace x2::ui {
namespace {

constexpr const char *kExtractionRevive = "gameplay-extraction-revive";

} // namespace

std::string
gameplay_settings_document_rml(const x2::config::Settings &settings) {
  std::ostringstream rml;
  rml << "<pane><div class='section-heading'>Gameplay</div>"
      << "<select-button id='" << kExtractionRevive
      << "'><key>Extraction point revive</key><value>"
      << extraction_revive_label(settings.extraction_revive)
      << "</value></select-button>"
      << "<div class='help'>Off keeps the original rule: a fallen hero stays "
         "down. Free revives every fallen hero and refills health and energy "
         "when the party reaches an extraction point. Paid offers to revive "
         "the "
         "fallen near an extraction point for the original revive cost: press "
         "F3, the controller's left shoulder (LB), or tap the prompt.</div>"
      << "<spacer></spacer></pane>";
  return rml.str();
}

void gameplay_settings_document_wire(Rml::ElementDocument &document,
                                     Rml::EventListener &listener) {
  if (Rml::Element *element = document.GetElementById(kExtractionRevive)) {
    element->AddEventListener("click", &listener);
    element->AddEventListener("keydown", &listener);
  }
}

bool gameplay_settings_document_change(x2::config::Settings &settings,
                                       const std::string &id) {
  if (id != kExtractionRevive)
    return false;
  settings.extraction_revive = static_cast<x2::config::ExtractionRevive>(
      (static_cast<unsigned>(settings.extraction_revive) + 1u) %
      (static_cast<unsigned>(x2::config::ExtractionRevive::Paid) + 1u));
  return true;
}

} // namespace x2::ui
