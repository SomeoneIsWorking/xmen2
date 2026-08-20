/*
 * RmlUi/SDL_GPU lifetime and host event bridge. The settings document and its
 * interaction live in settings_document.cpp, matching Dusklight's separation
 * between UI runtime and individual documents.
 */
#include "rmlui_ui.h"

#include <RmlUi/Core.h>
#include <RmlUi_Platform_SDL.h>
#include <RmlUi_Renderer_SDL_GPU.h>
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <memory>

#include "settings_document.hpp"

namespace {

std::unique_ptr<SystemInterface_SDL> system_interface;
std::unique_ptr<RenderInterface_SDL_GPU> render_interface;
Rml::Context* context;
SDL_Window* host_window;
bool initialized;
bool visible;

bool gamepad_navigation(const SDL_Event& event)
{
    if (!initialized || (event.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                         event.type != SDL_EVENT_GAMEPAD_BUTTON_UP))
        return false;
    Rml::Input::KeyIdentifier key = Rml::Input::KI_UNKNOWN;
    int modifiers = 0;
    switch (event.gbutton.button) {
    /* RmlUi has browser-style focus traversal but no default spatial focus
       navigation. Dusklight owns that translation explicitly too. Map the
       four directions onto forward/backward traversal so every control in
       this document remains reachable with a pad. */
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        key = Rml::Input::KI_TAB;
        modifiers = Rml::Input::KM_SHIFT;
        break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        key = Rml::Input::KI_TAB;
        break;
    case SDL_GAMEPAD_BUTTON_SOUTH: key = Rml::Input::KI_RETURN; break;
    case SDL_GAMEPAD_BUTTON_EAST:
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            visible = false;
            x2::ui::settings_document_cancel_capture();
        }
        return true;
    default: return false;
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
        context->ProcessKeyDown(key, modifiers);
    else
        context->ProcessKeyUp(key, modifiers);
    return true;
}

void discard_partial_initialization()
{
    x2::ui::settings_document_shutdown();
    context = nullptr;
    Rml::Shutdown();
    if (render_interface) render_interface->Shutdown();
    render_interface.reset();
    system_interface.reset();
    host_window = nullptr;
}

bool initialize(SDL_GPUDevice* device, SDL_Window* window, unsigned width,
                unsigned height)
{
    if (initialized) return true;
    host_window = window;
    system_interface = std::make_unique<SystemInterface_SDL>(window);
    render_interface = std::make_unique<RenderInterface_SDL_GPU>(device, window);
    Rml::SetSystemInterface(system_interface.get());
    Rml::SetRenderInterface(render_interface.get());
    if (!Rml::Initialise()) {
        render_interface.reset();
        system_interface.reset();
        return false;
    }
    if (!Rml::LoadFontFace(X2_UI_FONT_PATH))
        std::fprintf(stderr, "RMLUI: could not load font %s\n", X2_UI_FONT_PATH);
    if (!Rml::LoadFontFace(X2_UI_FONT_BOLD_PATH))
        std::fprintf(stderr, "RMLUI: could not load font %s\n",
                     X2_UI_FONT_BOLD_PATH);
    context = Rml::CreateContext("x2-settings",
                                 Rml::Vector2i((int)width, (int)height));
    if (!context || !x2::ui::settings_document_load(context, window)) {
        discard_partial_initialization();
        return false;
    }
    initialized = true;
    std::fprintf(stderr, "RMLUI: settings overlay initialized; F1 toggles it.\n");
    return true;
}

} // namespace

extern "C" int x2_ui_handle_event(SDL_Event* event)
{
    if (!event) return 0;
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
        event->key.key == SDLK_F1) {
        visible = !visible;
        if (initialized) x2::ui::settings_document_cancel_capture();
        return 1;
    }
    if (!visible) return 0;
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
        event->key.key == SDLK_ESCAPE) {
        if (x2::ui::settings_document_capturing())
            x2::ui::settings_document_cancel_capture();
        else
            visible = false;
        return 1;
    }
    if (x2::ui::settings_document_handle_event(*event)) return 1;
    if (gamepad_navigation(*event)) return 1;
    if (initialized) {
        RmlSDL::InputEventHandler(context, host_window, *event);
        if (x2::ui::settings_document_take_close_request()) visible = false;
    }
    return 1;
}

extern "C" int x2_ui_captures_input(void)
{
    return visible ? 1 : 0;
}

extern "C" void x2_ui_render(SDL_GPUDevice* device,
                             SDL_GPUCommandBuffer* command_buffer,
                             SDL_GPUTexture* swapchain, uint32_t width,
                             uint32_t height, SDL_Window* window)
{
    static bool environment_checked;
    if (!environment_checked) {
        const char* open = std::getenv("X2_SETTINGS_OPEN");
        visible = open && open[0] && open[0] != '0';
        environment_checked = true;
        if (visible)
            std::fprintf(stderr, "RMLUI: X2_SETTINGS_OPEN requested the "
                                 "settings overlay at startup.\n");
    }
    if (!visible || !device || !command_buffer || !swapchain || !window) return;
    if (!initialize(device, window, width, height)) return;
    context->SetDimensions(Rml::Vector2i((int)width, (int)height));
    /* Update can compile geometry and upload font textures. The SDL_GPU
       backend must already own this frame's command buffer before that work;
       doing BeginFrame after Update records uploads against stale state and
       corrupts both UI vertices and the frame under the overlay. */
    render_interface->BeginFrame(command_buffer, swapchain, width, height);
    context->Update();
    context->Render();
    render_interface->EndFrame();
}

extern "C" void x2_ui_gpu_shutdown(void)
{
    if (!initialized) return;
    x2::ui::settings_document_shutdown();
    context = nullptr;
    Rml::Shutdown();
    render_interface->Shutdown();
    render_interface.reset();
    system_interface.reset();
    host_window = nullptr;
    initialized = false;
}
