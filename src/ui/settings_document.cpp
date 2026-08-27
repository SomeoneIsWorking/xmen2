/*
 * The shipped settings document. Following Dusklight's document split, this
 * owns RML construction and interaction only; the RmlUi/SDL_GPU lifetime is
 * in rmlui_ui.cpp, persistence is in config/, presentation policy is in
 * presentation/, and publication into the guest is in input/.
 */
#include "settings_document.hpp"
#include "controller_assignment_rows.hpp"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "dinput_pad.h"
#include "dinput_system.h"
#include "gpu_shadow.h"
#include "binding_rows.h"
#include "live_resolution.h"
#include "settings_store.h"
#include "transient_controller_assignment.h"
#include "window_settings.h"
}

namespace x2::ui {
namespace {

Rml::ElementDocument* document;
SDL_Window* host_window;
unsigned selected_profile;
unsigned active_tab;
int capture_row = -1;
bool close_requested;
uint64_t observed_pad_generation;
std::vector<ControllerAssignmentRow> visible_controllers;

class SettingsListener final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override;
};
SettingsListener listener;

std::string escape_rml(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '\"') out += "&quot;";
        else out += c;
    }
    return out;
}

const char* keyboard_code_name(unsigned code)
{
    if (code > 0xffu) return nullptr;
    const char* name = dinput_system_dik_name((unsigned char)code);
    return name && name[0] ? name : nullptr;
}

std::string binding_label(const X2KeyboardProfile& profile, unsigned row)
{
    char out[64];
    if (!profile.keyboard_set[row]) return "Game default";
    if (!profile.keyboard[row]) return "Unbound";
    if (const char* name = keyboard_code_name(profile.keyboard[row])) return name;
    std::snprintf(out, sizeof out, "DIK 0x%02x", profile.keyboard[row]);
    return out;
}

void wire(const char* id, const char* event)
{
    if (Rml::Element* element = document->GetElementById(id))
        element->AddEventListener(event, &listener);
}

void rebuild()
{
    X2Settings* settings = x2_settings_store();
    const X2KeyboardProfile& profile =
        settings->keyboard_profile[selected_profile];
    Rml::Element* content;
    std::ostringstream rml;
    unsigned row;

    if (!document || !(content = document->GetElementById("content"))) return;
    dinput_pad_refresh();
    observed_pad_generation = dinput_pad_generation();
    if (active_tab == 0) {
        rml << "<pane><div class='section-heading'>Startup</div>"
               "<select-button id='boot-mode'><key>Boot</key><value>"
            << escape_rml(x2_boot_mode_label(settings->boot_mode))
            << "</value></select-button>"
               "<p id='status' class='status'></p><spacer></spacer></pane>"
               "<pane><div class='section-heading'>Boot behavior</div>"
               "<div class='help'>Normal plays the retail introduction. Menu "
               "skips the introduction and opens the retail main menu. "
               "Continue loads the newest save; if none exists it opens the "
               "main menu instead.</div><spacer></spacer></pane>";
    } else if (active_tab == 1) {
        rml << "<pane><div class='section-heading'>Display</div>";
        rml << "<select-button id='resolution'><key>Resolution</key><value>"
            << settings->width << "x" << settings->height
            << "</value></select-button>"
            << "<select-button id='window-mode'><key>Window mode</key><value>"
            << escape_rml(x2_window_mode_name(settings->window_mode))
            << "</value></select-button>"
            << "<select-button id='dynamic-shadows'><key>Dynamic shadows</key><value>"
            << (settings->dynamic_shadows ? "On" : "Off")
            << "</value></select-button>"
            << "<select-button id='shadow-resolution'><key>Shadow quality</key><value>"
            << settings->shadow_resolution << "</value></select-button>"
            << "<p id='status' class='status'></p><spacer></spacer></pane>"
            << "<pane><div class='section-heading'>Presentation</div>"
               "<div class='help'>Windowed uses the selected client size. "
               "Borderless uses the desktop mode. Exclusive fullscreen "
               "switches the display to the selected resolution.</div>"
               "<spacer></spacer></pane>";
    } else {
        visible_controllers = controller_assignment_rows(*settings);
        rml << "<pane><div class='section-heading'>Device assignments</div>"
               "<div class='help'>Player 1 may use keyboard and controller "
               "together. Players 2–4 use one device each and press Start to "
               "join. Session-only rows are not saved and temporarily "
               "override the saved device until cleared.</div>"
               "<div class='assignment-row assignment-head'><key>Device</key>"
               "<value>Off</value><value>P1</value><value>P2</value>"
               "<value>P3</value><value>P4</value></div>";
        for (unsigned p = 0; p < X2_SETTINGS_KEYBOARD_PROFILES; p++) {
            rml << "<div class='assignment-row'><key>Keyboard " << p + 1
                << "</key>";
            for (int owner = -1; owner < (int)X2_SETTINGS_PLAYERS; owner++)
                rml << "<button id='assign-kb-" << p << "-" << owner + 1
                    << "'>" << (settings->keyboard_player[p] == owner &&
                        !(owner > 0 && x2_transient_controller_has_assignment(owner))
                        ? "●" : "·")
                    << "</button>";
            rml << "</div>";
        }
        for (size_t i = 0; i < visible_controllers.size(); i++) {
            int assigned = visible_controllers[i].owner;
            rml << "<div class='assignment-row'><key>"
                << escape_rml(visible_controllers[i].name) << "</key>";
            for (int owner = -1; owner < (int)X2_SETTINGS_PLAYERS; owner++)
                rml << "<button id='assign-pad-" << i << "-" << owner + 1
                    << "'>" << (assigned == owner ? "●" : "·") << "</button>";
            rml << "</div>";
        }
        rml << "<p id='status' class='status'></p><spacer></spacer></pane>"
               "<pane><select-button id='profile'><key>Edit bindings</key>"
            << "<value>Keyboard " << selected_profile + 1
            << "</value></select-button><div class='hint'>Select a binding and "
               "press its replacement key. Delete clears it; Escape cancels."
               "</div><div class='section-heading'>Actions</div>";
        for (row = 0; row < INPUT_BINDING_ROWS; row++) {
            const char* name = input_binding_row_display_label(row);
            rml << "<div class='binding'><key>"
                << escape_rml(name ? name : "Unknown")
                << "</key><button id='kb-" << row << "'>"
                << binding_label(profile, row) << "</button></div>";
        }
        rml << "<p id='status' class='status'></p>"
               "<spacer></spacer></pane>";
    }
    content->SetInnerRML(rml.str());
    if (active_tab == 0) {
        wire("boot-mode", "click");
        wire("boot-mode", "keydown");
    } else if (active_tab == 1) {
        wire("resolution", "click");
        wire("resolution", "keydown");
        wire("window-mode", "click");
        wire("window-mode", "keydown");
        wire("dynamic-shadows", "click");
        wire("dynamic-shadows", "keydown");
        wire("shadow-resolution", "click");
        wire("shadow-resolution", "keydown");
    } else {
        wire("profile", "click");
        wire("profile", "keydown");
        for (unsigned p = 0; p < X2_SETTINGS_KEYBOARD_PROFILES; p++)
            for (int owner = 0; owner <= (int)X2_SETTINGS_PLAYERS; owner++) {
                std::string id = "assign-kb-" + std::to_string(p) + "-" +
                                 std::to_string(owner);
                wire(id.c_str(), "click");
            }
        for (size_t i = 0; i < visible_controllers.size(); i++)
            for (int owner = 0; owner <= (int)X2_SETTINGS_PLAYERS; owner++) {
                std::string id = "assign-pad-" + std::to_string(i) + "-" +
                                 std::to_string(owner);
                wire(id.c_str(), "click");
            }
    }
    for (row = 0; row < INPUT_BINDING_ROWS; row++) {
        std::string kb = "kb-" + std::to_string(row);
        wire(kb.c_str(), "click");
    }
}

void set_status(const std::string& status)
{
    if (document)
        if (Rml::Element* element = document->GetElementById("status"))
            element->SetInnerRML(escape_rml(status));
}

std::string save_settings()
{
    char why[256];
    if (!x2_settings_store_save(why, sizeof why)) return why;
    return "Saved";
}

void SettingsListener::ProcessEvent(Rml::Event& event)
{
    Rml::Element* element = event.GetCurrentElement();
    if (!element) return;
    std::string id = element->GetId();
    if (event.GetId() == Rml::EventId::Keydown &&
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN) !=
            Rml::Input::KI_RETURN)
        return;
    if (id == "boot-mode") {
        X2Settings* settings = x2_settings_store();
        X2BootMode before = settings->boot_mode;
        char why[256];
        bool saved;
        settings->boot_mode = (X2BootMode)(
            ((unsigned)settings->boot_mode + 1u) % 3u);
        saved = x2_settings_store_save(why, sizeof why);
        if (!saved) settings->boot_mode = before;
        rebuild();
        set_status(saved ? "Saved" : why);
    } else if (id == "dynamic-shadows" || id == "shadow-resolution") {
        X2Settings* settings = x2_settings_store();
        X2Settings before = *settings;
        char why[256];
        if (id == "dynamic-shadows") settings->dynamic_shadows ^= 1u;
        else settings->shadow_resolution = settings->shadow_resolution == 4096
            ? 512 : (uint16_t)(settings->shadow_resolution * 2u);
        bool saved = x2_settings_store_save(why, sizeof why);
        if (!saved) *settings = before;
        gpu_shadow_configure(settings->dynamic_shadows, settings->shadow_resolution);
        rebuild();
        set_status(saved ? "Saved" : why);
    } else if (id == "resolution") {
        X2Settings* settings = x2_settings_store();
        X2Settings before = *settings;
        char why[256];
        x2_live_resolution_select_next(settings);
        bool applied = x2_live_resolution_apply(host_window, settings, &before,
                                                why, sizeof why);
        rebuild();
        set_status(why);
        if (!applied) return;
    } else if (id == "window-mode") {
        X2Settings* settings = x2_settings_store();
        X2Settings before = *settings;
        char why[256];
        settings->window_mode = (X2WindowMode)(
            ((unsigned)settings->window_mode + 1u) % 3u);
        if (!x2_window_settings_apply(host_window, settings, why, sizeof why)) {
            char rollback_why[256];
            *settings = before;
            if (!x2_window_settings_apply(host_window, &before, rollback_why,
                                          sizeof rollback_why))
                std::fprintf(stderr, "RMLUI: display rollback also failed: %s\n",
                             rollback_why);
            set_status(why);
        } else {
            std::string status = save_settings();
            rebuild();
            set_status(status);
        }
    } else if (id == "close") {
        close_requested = true;
    } else if (id.rfind("tab-", 0) == 0) {
        active_tab = id == "tab-general" ? 0 : id == "tab-video" ? 1 : 2;
        if (Rml::Element* tab = document->GetElementById("tab-general"))
            tab->SetPseudoClass("selected", active_tab == 0);
        if (Rml::Element* tab = document->GetElementById("tab-video"))
            tab->SetPseudoClass("selected", active_tab == 1);
        if (Rml::Element* tab = document->GetElementById("tab-input"))
            tab->SetPseudoClass("selected", active_tab == 2);
        rebuild();
    } else if (id.rfind("assign-kb-", 0) == 0) {
        unsigned profile_index, owner_index;
        if (std::sscanf(id.c_str(), "assign-kb-%u-%u", &profile_index,
                        &owner_index) != 2 ||
            !x2_settings_assign_keyboard(x2_settings_store(), profile_index,
                                         (int)owner_index - 1))
            return;
        if (owner_index > 1)
            x2_transient_controller_clear_player(owner_index - 1);
        std::string status = save_settings();
        rebuild();
        set_status(status);
    } else if (id.rfind("assign-pad-", 0) == 0) {
        unsigned controller_index, owner_index;
        if (std::sscanf(id.c_str(), "assign-pad-%u-%u", &controller_index,
                        &owner_index) != 2 ||
            controller_index >= visible_controllers.size())
            return;
        ControllerAssignmentRow& controller = visible_controllers[controller_index];
        if (controller.transient_assignment || !controller.stable_identity) {
            int owner = (int)owner_index - 1;
            if (owner < 0 && controller.owner >= 0)
                x2_transient_controller_clear_player(
                    (unsigned)controller.owner);
            else if (owner < 0 || controller.pad < 0 ||
                     !x2_transient_controller_assign(controller.pad,
                                                     (unsigned)owner))
                return;
            rebuild();
            set_status(owner < 0 ? "Cleared session assignment" :
                       "Assigned for this session only; not saved");
            return;
        }
        if (!x2_settings_assign_controller(
                x2_settings_store(), controller.id.c_str(),
                (int)owner_index - 1))
            return;
        if (owner_index > 0)
            x2_transient_controller_clear_player(owner_index - 1);
        std::string status = save_settings();
        rebuild();
        set_status(status);
    } else if (id == "profile") {
        selected_profile = (selected_profile + 1u) %
                           X2_SETTINGS_KEYBOARD_PROFILES;
        rebuild();
    } else if (id.rfind("kb-", 0) == 0) {
        capture_row = std::atoi(id.c_str() + 3);
        element->SetInnerRML("Press a control...");
    }
}

bool capture(const SDL_Event& event)
{
    if (capture_row < 0) return false;
    X2Settings* settings = x2_settings_store();
    X2KeyboardProfile& profile =
        settings->keyboard_profile[selected_profile];
    int code = 0;
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.key == SDLK_DELETE) {
        profile.keyboard[capture_row] = 0;
        profile.keyboard_set[capture_row] = 1;
        capture_row = -1;
        std::string status = save_settings();
        rebuild();
        set_status(status);
        return true;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        code = dinput_system_dik(event.key.scancode);
    if (!code) return true;
    profile.keyboard[capture_row] = (uint16_t)code;
    profile.keyboard_set[capture_row] = 1;
    capture_row = -1;
    std::string status = save_settings();
    rebuild();
    set_status(status);
    return true;
}

} // namespace

bool settings_document_load(Rml::Context* context, SDL_Window* window)
{
    static const char* shell = R"RML(
<rml><head><title>X-Men Legends II Settings</title>
<link type="text/rcss" href="settings.rcss" /></head>
<body><window id="window" open>
<tab-bar closable>
<tab id="tab-general">General</tab>
<tab id="tab-video">Video</tab>
<tab id="tab-input">Input</tab>
<tab-end-spacer></tab-end-spacer><close id="close"></close>
</tab-bar><content id="content"></content>
</window></body></rml>)RML";
    host_window = window;
    document = context->LoadDocumentFromMemory(shell, X2_UI_DOCUMENT_URL);
    if (!document) return false;
    document->Show();
    wire("close", "click");
    wire("close", "keydown");
    wire("tab-general", "click");
    wire("tab-general", "keydown");
    wire("tab-video", "click");
    wire("tab-video", "keydown");
    wire("tab-input", "click");
    wire("tab-input", "keydown");
    if (Rml::Element* tab = document->GetElementById("tab-general"))
        tab->SetPseudoClass("selected", true);
    rebuild();
    return true;
}

void settings_document_shutdown()
{
    document = nullptr;
    host_window = nullptr;
    selected_profile = 0;
    active_tab = 0;
    capture_row = -1;
    close_requested = false;
    observed_pad_generation = 0;
    visible_controllers.clear();
}

bool settings_document_handle_event(const SDL_Event& event)
{
    return capture(event);
}

void settings_document_update()
{
    uint64_t generation;
    dinput_pad_refresh();
    generation = dinput_pad_generation();
    if (generation == observed_pad_generation) return;
    observed_pad_generation = generation;
    if (document && active_tab == 2 && capture_row < 0) rebuild();
}

bool settings_document_capturing()
{
    return capture_row >= 0;
}

void settings_document_cancel_capture()
{
    capture_row = -1;
    rebuild();
}

bool settings_document_take_close_request()
{
    bool requested = close_requested;
    close_requested = false;
    return requested;
}

} // namespace x2::ui
