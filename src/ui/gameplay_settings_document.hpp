#pragma once

#include "settings.h"

#include <string>

namespace Rml {
class ElementDocument;
class EventListener;
} // namespace Rml

namespace x2::ui {

std::string gameplay_settings_document_rml(const X2Settings &settings);
void gameplay_settings_document_wire(Rml::ElementDocument &document,
                                     Rml::EventListener &listener);
/* Steps the setting the control `id` names; false when `id` is not one. */
bool gameplay_settings_document_change(X2Settings &settings,
                                       const std::string &id);

} // namespace x2::ui
