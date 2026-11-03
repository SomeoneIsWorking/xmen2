/*
 * The shipped settings document owns RML construction and interaction only.
 * rmlui_ui.cpp owns RmlUi/SDL_GPU lifetime, config/ owns persistence,
 * presentation/ owns presentation policy, and input/ owns guest publication.
 */
#include "settings_document.hpp"
#include "controller_assignment_rows.hpp"
#include "keyboard_bindings_document.hpp"
#include "rml_text.hpp"
#include "ui_resources.h"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "dinput_pad.h"
#include "dinput_system.h"
#include "settings_store.h"
#include "transient_controller_assignment.h"
#include "x2_log.h"

namespace x2::ui {
namespace {

Rml::ElementDocument *document;
SDL_Window *host_window;
unsigned selected_profile;
int capture_row = -1;
bool close_requested;
uint64_t observed_pad_generation;
bool observed_game_bindings;
std::vector<ControllerAssignmentRow> visible_controllers;

class SettingsListener final : public Rml::EventListener {
public:
  void ProcessEvent(Rml::Event &event) override;
};
SettingsListener listener;

void assignment_cell(std::ostringstream &rml, const char *kind, size_t row,
                     int owner, bool assigned) {
  rml << "<button id='assign-" << kind << "-" << row << "-" << owner + 1 << "'"
      << (assigned ? " class='assigned'" : "") << "><img src='"
      << (assigned ? "assign_on.svg" : "assign_off.svg") << "' /></button>";
}

/* A session pad displaced from `seat` takes the seat its displacer left. */
void relocate_session_pad(unsigned seat, int vacated) {
  if (!x2::input::transient_controller_has_assignment(seat))
    return;
  if (vacated < 0 || (unsigned)vacated == seat ||
      !x2::input::transient_controller_move(seat, (unsigned)vacated))
    x2::input::transient_controller_clear_player(seat);
}

void wire(const char *id, const char *event) {
  if (Rml::Element *element = document->GetElementById(id))
    element->AddEventListener(event, &listener);
}

void rebuild() {
  x2::config::Settings *settings = x2::config::settings_store();
  const x2::config::KeyboardProfile &profile =
      settings->keyboard_profile[selected_profile];
  Rml::Element *content;
  std::ostringstream rml;

  if (!document || !(content = document->GetElementById("content")))
    return;
  dinput_pad_refresh();
  observed_pad_generation = dinput_pad_generation();
  observed_game_bindings = keyboard_bindings_game_known();
  {
    visible_controllers = controller_assignment_rows(*settings);
    rml << "<pane><div class='section-heading'>Device assignments</div>"
           "<div class='help'>Player 1 may use keyboard and controller "
           "together. Players 2–4 use one device each and press Start to "
           "join. Session-only rows are not saved and temporarily "
           "override the saved device until cleared.</div>"
           "<div class='assignment-row assignment-head'><key>Device</key>"
           "<value>Off</value><value>P1</value><value>P2</value>"
           "<value>P3</value><value>P4</value></div>";
    for (unsigned p = 0; p < x2::config::kSettingsKeyboardProfiles; p++) {
      rml << "<div class='assignment-row'><key>Keyboard " << p + 1 << "</key>";
      for (int owner = -1; owner < (int)x2::config::kSettingsPlayers; owner++)
        assignment_cell(
            rml, "kb", p, owner,
            settings->keyboard_player[p] == owner &&
                !(owner > 0 &&
                  x2::input::transient_controller_has_assignment(owner)));
      rml << "</div>";
    }
    for (size_t i = 0; i < visible_controllers.size(); i++) {
      int assigned = visible_controllers[i].owner;
      rml << "<div class='assignment-row'><key>"
          << escape_rml(visible_controllers[i].name) << "</key>";
      for (int owner = -1; owner < (int)x2::config::kSettingsPlayers; owner++)
        assignment_cell(rml, "pad", i, owner, assigned == owner);
      rml << "</div>";
    }
    rml << "<p id='status' class='status'></p><spacer></spacer></pane>"
           "<pane><select-button id='profile'><key>Edit bindings</key>"
        << "<value>Keyboard " << selected_profile + 1
        << "</value></select-button><div class='hint'>Select a binding and "
           "press its replacement key. Delete clears it; Escape cancels. "
           "Reset returns a binding to the game's own key.</div>"
        << keyboard_bindings_document_rml(profile)
        << "<p id='status' class='status'></p>"
           "<spacer></spacer></pane>";
  }
  content->SetInnerRML(rml.str());
  {
    wire("profile", "click");
    wire("profile", "keydown");
    for (unsigned p = 0; p < x2::config::kSettingsKeyboardProfiles; p++)
      for (int owner = 0; owner <= (int)x2::config::kSettingsPlayers; owner++) {
        std::string id =
            "assign-kb-" + std::to_string(p) + "-" + std::to_string(owner);
        wire(id.c_str(), "click");
      }
    for (size_t i = 0; i < visible_controllers.size(); i++)
      for (int owner = 0; owner <= (int)x2::config::kSettingsPlayers; owner++) {
        std::string id =
            "assign-pad-" + std::to_string(i) + "-" + std::to_string(owner);
        wire(id.c_str(), "click");
      }
    keyboard_bindings_document_wire(*document, listener);
  }
}

void set_status(const std::string &status) {
  if (document)
    if (Rml::Element *element = document->GetElementById("status"))
      element->SetInnerRML(escape_rml(status));
}

std::string save_settings() {
  char why[256];
  if (!x2::config::settings_store_save(why, sizeof why))
    return why;
  return "Saved";
}

void SettingsListener::ProcessEvent(Rml::Event &event) {
  Rml::Element *element = event.GetCurrentElement();
  if (!element)
    return;
  std::string id = element->GetId();
  if (event.GetId() == Rml::EventId::Keydown &&
      event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN) !=
          Rml::Input::KI_RETURN)
    return;
  if (id == "close") {
    close_requested = true;
  } else if (id.rfind("assign-kb-", 0) == 0) {
    x2::config::Settings *settings = x2::config::settings_store();
    unsigned profile_index, owner_index;
    if (std::sscanf(id.c_str(), "assign-kb-%u-%u", &profile_index,
                    &owner_index) != 2 ||
        profile_index >= x2::config::kSettingsKeyboardProfiles)
      return;
    int vacated = settings->keyboard_player[profile_index];
    if (!settings_assign_keyboard(settings, profile_index,
                                  (int)owner_index - 1))
      return;
    if (owner_index > 1)
      relocate_session_pad(owner_index - 1, vacated);
    std::string status = save_settings();
    rebuild();
    set_status(status);
  } else if (id.rfind("assign-pad-", 0) == 0) {
    unsigned controller_index, owner_index;
    if (std::sscanf(id.c_str(), "assign-pad-%u-%u", &controller_index,
                    &owner_index) != 2 ||
        controller_index >= visible_controllers.size())
      return;
    ControllerAssignmentRow &controller = visible_controllers[controller_index];
    if (controller.transient_assignment || !controller.stable_identity) {
      int owner = (int)owner_index - 1;
      if (owner < 0 && controller.owner >= 0)
        x2::input::transient_controller_clear_player(
            (unsigned)controller.owner);
      else if (owner < 0 || controller.pad < 0 ||
               !x2::input::transient_controller_assign(controller.pad,
                                                       (unsigned)owner))
        return;
      rebuild();
      set_status(owner < 0 ? "Cleared session assignment"
                           : "Assigned for this session only; not saved");
      return;
    }
    if (!settings_assign_controller(x2::config::settings_store(),
                                    controller.id.c_str(),
                                    (int)owner_index - 1))
      return;
    if (owner_index > 0)
      relocate_session_pad(owner_index - 1, controller.owner);
    std::string status = save_settings();
    rebuild();
    set_status(status);
  } else if (id == "profile") {
    selected_profile =
        (selected_profile + 1u) % x2::config::kSettingsKeyboardProfiles;
    rebuild();
  } else if (id.rfind("binding-reset-", 0) == 0) {
    x2::config::Settings *settings = x2::config::settings_store();
    x2::config::KeyboardProfile &profile =
        settings->keyboard_profile[selected_profile];
    x2::config::KeyboardProfile before = profile;
    if (!keyboard_bindings_document_restore(profile, id))
      return;
    std::string status = save_settings();
    if (status != "Saved")
      profile = before;
    rebuild();
    set_status(status);
  } else if (id.rfind("kb-", 0) == 0) {
    capture_row = std::atoi(id.c_str() + 3);
    element->SetInnerRML("Press a control...");
  }
}

bool capture(const SDL_Event &event) {
  if (capture_row < 0)
    return false;
  x2::config::Settings *settings = x2::config::settings_store();
  x2::config::KeyboardProfile &profile =
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
  if (!code)
    return true;
  profile.keyboard[capture_row] = (uint16_t)code;
  profile.keyboard_set[capture_row] = 1;
  capture_row = -1;
  std::string status = save_settings();
  rebuild();
  set_status(status);
  return true;
}

} // namespace

bool settings_document_load(Rml::Context *context, SDL_Window *window) {
  static const char *shell = R"RML(
<rml><head><title>X-Men Legends II Settings</title>
<link type="text/rcss" href="settings.rcss" /></head>
<body><window id="window" open>
<tab-bar closable>
<tab id="tab-input">Controls</tab>
<tab-end-spacer></tab-end-spacer><close id="close"></close>
</tab-bar><content id="content"></content>
</window></body></rml>)RML";
  host_window = window;
  document = context->LoadDocumentFromMemory(
      shell, x2::ui::ui_resource_path("settings.rml"));
  if (!document)
    return false;
  document->Show();
  wire("close", "click");
  wire("close", "keydown");
  if (Rml::Element *tab = document->GetElementById("tab-input"))
    tab->SetPseudoClass("selected", true);
  rebuild();
  return true;
}

void settings_document_shutdown() {
  document = nullptr;
  host_window = nullptr;
  selected_profile = 0;
  capture_row = -1;
  close_requested = false;
  observed_pad_generation = 0;
  observed_game_bindings = false;
  visible_controllers.clear();
}

void settings_document_set_visible(bool visible) {
  if (!document)
    return;
  if (visible)
    document->Show();
  else
    document->Hide();
}

bool settings_document_handle_event(const SDL_Event &event) {
  return capture(event);
}

void settings_document_update() {
  uint64_t generation;
  dinput_pad_refresh();
  generation = dinput_pad_generation();
  if (generation == observed_pad_generation &&
      keyboard_bindings_game_known() == observed_game_bindings)
    return;
  observed_pad_generation = generation;
  observed_game_bindings = keyboard_bindings_game_known();
  if (document && capture_row < 0)
    rebuild();
}

bool settings_document_capturing() { return capture_row >= 0; }

void settings_document_cancel_capture() {
  capture_row = -1;
  rebuild();
}

bool settings_document_take_close_request() {
  bool requested = close_requested;
  close_requested = false;
  return requested;
}

} // namespace x2::ui
