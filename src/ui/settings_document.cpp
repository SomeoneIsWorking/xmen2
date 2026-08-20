/*
 * The shipped settings document. Following Dusklight's document split, this
 * owns RML construction and interaction only; the RmlUi/SDL_GPU lifetime is
 * in rmlui_ui.cpp, persistence is in config/, presentation policy is in
 * presentation/, and publication into the guest is in input/.
 */
#include "settings_document.hpp"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

extern "C" {
#include "dinput_pad.h"
#include "dinput_system.h"
#include "input_bindings.h"
#include "settings_store.h"
#include "window_settings.h"
}

namespace x2::ui {
namespace {

Rml::ElementDocument* document;
SDL_Window* host_window;
unsigned selected_player;
unsigned active_tab;
int capture_row = -1;
bool close_requested;

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
    if (!code) return "Unbound";
    for (int scancode = SDL_SCANCODE_UNKNOWN + 1;
         scancode < SDL_SCANCODE_COUNT; scancode++)
        if (dinput_system_dik(scancode) == code) {
            const char* name = SDL_GetScancodeName((SDL_Scancode)scancode);
            if (name && name[0]) return name;
        }
    return nullptr;
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
    const X2PlayerSettings& player = settings->player[selected_player];
    const X2KeyboardProfile& profile =
        settings->keyboard_profile[player.keyboard_profile];
    Rml::Element* content;
    std::ostringstream rml;
    unsigned row;
    int pad;

    if (!document || !(content = document->GetElementById("content"))) return;
    if (active_tab == 0) {
        rml << "<pane><div class='section-heading'>Display</div>";
        rml << "<select-button id='resolution'><key>Resolution</key><value>"
            << settings->width << "x" << settings->height
            << "</value></select-button>"
            << "<select-button id='window-mode'><key>Window mode</key><value>"
            << escape_rml(x2_window_mode_name(settings->window_mode))
            << "</value></select-button>"
            << "<p id='status' class='status'></p><spacer></spacer></pane>"
            << "<pane><div class='section-heading'>Presentation</div>"
               "<div class='help'>Windowed uses the selected client size. "
               "Borderless uses the desktop mode. Exclusive fullscreen "
               "switches the display to the selected resolution.</div>"
               "<spacer></spacer></pane>";
    } else {
        std::string device_label = x2_player_device_name(player.type);
        dinput_pad_refresh();
        for (pad = 0; pad < DINPUT_PAD_MAX; pad++) {
            const char* id = dinput_pad_persistent_id(pad);
            const char* name = dinput_pad_name(pad);
            if (id && name && player.type == X2_PLAYER_GAMEPAD &&
                std::strcmp(player.id, id) == 0)
                device_label = name;
        }
        if (player.type == X2_PLAYER_GAMEPAD && device_label == "gamepad")
            device_label = "Disconnected gamepad";
        rml << "<pane><div class='section-heading'>Player "
            << selected_player + 1 << "</div>"
            << "<select-button id='device'><key>Device</key><value>"
            << escape_rml(device_label) << "</value></select-button>";
        if (player.type == X2_PLAYER_GAMEPAD) {
            rml << "<div class='section-heading'>Controller layout</div>"
                   "<div class='help'>Controllers use the canonical Xbox/PS2 "
                   "layout. Assign a physical controller to a player here; "
                   "the gameplay bindings are fixed.</div>"
                   "<spacer></spacer></pane><pane>"
                   "<div class='section-heading'>Standard mapping</div>"
                   "<div class='help'>Movement and camera use the sticks. "
                   "Face buttons, triggers, shoulders, Start, Back and "
                   "stick clicks follow the original console defaults.</div>";
        } else if (player.type == X2_PLAYER_NONE) {
            rml << "<div class='help'>This player has no input device. "
                   "Choose Keyboard or a connected controller to enable it."
                   "</div><spacer></spacer></pane><pane>";
        } else {
            rml << "<select-button id='profile'><key>Keyboard profile</key>"
                << "<value>Keyboard " << player.keyboard_profile + 1
                << "</value></select-button>"
                   "<div class='hint'>Keyboard profiles are reusable: assign "
                   "different profiles to players sharing one keyboard. "
                   "Select a binding and press its replacement key. Delete "
                   "clears it; Escape cancels.</div>"
                   "<div class='section-heading'>Actions</div>";
            for (row = 0; row < INPUT_BINDING_ROWS; row++) {
                if (row == (INPUT_BINDING_ROWS + 1) / 2)
                    rml << "<spacer></spacer></pane><pane>"
                           "<div class='section-heading'>Actions</div>";
                const char* name = input_binding_row_name(row);
                rml << "<div class='binding'><key>"
                    << escape_rml(name ? name : "Unknown")
                    << "</key><button id='kb-" << row << "'>"
                    << binding_label(profile, row) << "</button></div>";
            }
        }
        rml << "<p id='status' class='status'></p>"
               "<spacer></spacer></pane>";
    }
    content->SetInnerRML(rml.str());
    if (active_tab == 0) {
        wire("resolution", "click");
        wire("resolution", "keydown");
        wire("window-mode", "click");
        wire("window-mode", "keydown");
    } else {
        wire("device", "click");
        wire("device", "keydown");
        wire("profile", "click");
        wire("profile", "keydown");
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
    if (id == "resolution" || id == "window-mode") {
        X2Settings* settings = x2_settings_store();
        X2Settings before = *settings;
        char why[256];
        if (id == "resolution") {
            static const unsigned resolutions[][2] = {
                {1280, 720}, {1600, 900}, {1920, 1080},
                {2560, 1440}, {3840, 2160}
            };
            unsigned next = 0;
            for (unsigned i = 0; i < sizeof resolutions / sizeof resolutions[0]; i++)
                if (settings->width == resolutions[i][0] &&
                    settings->height == resolutions[i][1])
                    next = (i + 1) % (sizeof resolutions / sizeof resolutions[0]);
            settings->width = resolutions[next][0];
            settings->height = resolutions[next][1];
        } else {
            settings->window_mode = (X2WindowMode)(
                ((unsigned)settings->window_mode + 1u) % 3u);
        }
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
        if (id == "tab-video") {
            active_tab = 0;
        } else {
            selected_player = (unsigned)std::strtoul(id.c_str() + 11, nullptr, 10);
            active_tab = selected_player + 1;
        }
        for (unsigned i = 0; i <= X2_SETTINGS_PLAYERS; i++) {
            std::string tab_id = i == 0 ? "tab-video" :
                "tab-player-" + std::to_string(i - 1);
            if (Rml::Element* tab = document->GetElementById(tab_id))
                tab->SetPseudoClass("selected", i == active_tab);
        }
        rebuild();
    } else if (id == "device") {
        X2PlayerSettings& player = x2_settings_store()->player[selected_player];
        int first = -1, next = -1, current = -1;
        dinput_pad_refresh();
        for (int pad = 0; pad < DINPUT_PAD_MAX; pad++) {
            const char* persistent = dinput_pad_persistent_id(pad);
            if (!persistent) continue;
            if (first < 0) first = pad;
            if (current >= 0 && next < 0) next = pad;
            if (player.type == X2_PLAYER_GAMEPAD &&
                std::strcmp(player.id, persistent) == 0)
                current = pad;
        }
        if (player.type == X2_PLAYER_NONE) player.type = X2_PLAYER_AUTO;
        else if (player.type == X2_PLAYER_AUTO) player.type = X2_PLAYER_KEYBOARD;
        else if (player.type == X2_PLAYER_KEYBOARD && first >= 0) {
            player.type = X2_PLAYER_GAMEPAD;
            std::snprintf(player.id, sizeof player.id, "%s",
                          dinput_pad_persistent_id(first));
        } else if (player.type == X2_PLAYER_GAMEPAD && current < 0 && first >= 0) {
            std::snprintf(player.id, sizeof player.id, "%s",
                          dinput_pad_persistent_id(first));
        } else if (player.type == X2_PLAYER_GAMEPAD && next >= 0) {
            std::snprintf(player.id, sizeof player.id, "%s",
                          dinput_pad_persistent_id(next));
        } else {
            player.type = X2_PLAYER_NONE;
            player.id[0] = 0;
        }
        if (player.type != X2_PLAYER_GAMEPAD) player.id[0] = 0;
        std::string status = save_settings();
        rebuild();
        set_status(status);
    } else if (id == "profile") {
        X2PlayerSettings& player = x2_settings_store()->player[selected_player];
        player.keyboard_profile = (uint8_t)(
            (player.keyboard_profile + 1u) % X2_SETTINGS_KEYBOARD_PROFILES);
        std::string status = save_settings();
        rebuild();
        set_status(status);
    } else if (id.rfind("kb-", 0) == 0) {
        capture_row = std::atoi(id.c_str() + 3);
        element->SetInnerRML("Press a control...");
    }
}

bool capture(const SDL_Event& event)
{
    if (capture_row < 0) return false;
    X2Settings* settings = x2_settings_store();
    const X2PlayerSettings& player = settings->player[selected_player];
    X2KeyboardProfile& profile =
        settings->keyboard_profile[player.keyboard_profile];
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
<tab id="tab-video">Video</tab>
<tab id="tab-player-0">Player 1</tab><tab id="tab-player-1">Player 2</tab>
<tab id="tab-player-2">Player 3</tab><tab id="tab-player-3">Player 4</tab>
<tab-end-spacer></tab-end-spacer><close id="close"></close>
</tab-bar><content id="content"></content>
</window></body></rml>)RML";
    host_window = window;
    document = context->LoadDocumentFromMemory(shell, X2_UI_DOCUMENT_URL);
    if (!document) return false;
    document->Show();
    wire("close", "click");
    wire("close", "keydown");
    wire("tab-video", "click");
    wire("tab-video", "keydown");
    if (Rml::Element* tab = document->GetElementById("tab-video"))
        tab->SetPseudoClass("selected", true);
    for (unsigned i = 0; i < X2_SETTINGS_PLAYERS; i++) {
        std::string id = "tab-player-" + std::to_string(i);
        wire(id.c_str(), "click");
        wire(id.c_str(), "keydown");
    }
    rebuild();
    return true;
}

void settings_document_shutdown()
{
    document = nullptr;
    host_window = nullptr;
    selected_player = 0;
    active_tab = 0;
    capture_row = -1;
    close_requested = false;
}

bool settings_document_handle_event(const SDL_Event& event)
{
    return capture(event);
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
