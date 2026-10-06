#pragma once

#include "hud_settings.h"

#include <string>

namespace Rml {
class ElementDocument;
class EventListener;
} // namespace Rml

namespace x2::ui {

std::string hud_settings_document_rml(const X2HudSettings &settings);
void hud_settings_document_wire(Rml::ElementDocument &document,
                                Rml::EventListener &listener);
bool hud_settings_document_change(X2HudSettings &settings,
                                  const std::string &id);

} // namespace x2::ui
