#pragma once

#include "settings.h"

#include <string>

namespace Rml {
class ElementDocument;
class EventListener;
} // namespace Rml

namespace x2::ui {

/* The Display tab's rows: resolution, window mode, shadow controls, and the
   HUD sizing document composed below them. The tab's change handling stays
   with the settings document, which owns the host window and the status line
   a failed resolution change has to report into. */
std::string display_settings_document_rml(const X2Settings &settings);
void display_settings_document_wire(Rml::ElementDocument &document,
                                    Rml::EventListener &listener);

} // namespace x2::ui
