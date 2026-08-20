#pragma once

union SDL_Event;
struct SDL_Window;

namespace Rml {
class Context;
}

namespace x2::ui {

bool settings_document_load(Rml::Context* context, SDL_Window* window);
void settings_document_shutdown();
bool settings_document_handle_event(const SDL_Event& event);
bool settings_document_capturing();
void settings_document_cancel_capture();
bool settings_document_take_close_request();

} // namespace x2::ui
