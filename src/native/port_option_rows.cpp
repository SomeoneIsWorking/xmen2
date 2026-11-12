#include "port_option_rows.hpp"

#include "../input/binding_capture.hpp"
#include "../input/controller_assignment_rows.hpp"
#include "../input/keyboard_binding_labels.hpp"
#include "binding_rows.h"
#include "display_geometry.h"
#include "gpu_shadow.h"
#include "live_resolution.h"
#include "render_size.h"
#include "resolution_ladder.h"
#include "retail_front_end.hpp"
#include "settings_store.h"
#include "x2_log.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace x2::native {
namespace {

using x2::config::Settings;

constexpr std::string_view kItemPrefix = "port_option_";

/* A cyclic choice of `count` values stepped by `direction`. */
unsigned cycle(unsigned value, unsigned count, int direction) {
  return direction < 0 ? (value + count - 1u) % count : (value + 1u) % count;
}

/* A percentage stepped by `step` inside [low, high], wrapping at the ends. */
unsigned percent_step(unsigned value, unsigned low, unsigned high,
                      unsigned step, int direction) {
  if (direction > 0) {
    const unsigned next = low + ((value - low) / step + 1u) * step;
    return next > high ? low : next;
  }
  if (value <= low) {
    return low + ((high - low) / step) * step;
  }
  return low + ((value - low - 1u) / step) * step;
}

std::string percent(unsigned value) { return std::to_string(value) + "%"; }

/* Keyboards and controllers on the Controls page; more controllers than
   seats are never assignable at once. */
constexpr unsigned kControllerRows = 6u;
constexpr std::string_view kKeyboardKey = "keyboard_";
constexpr std::string_view kControllerKey = "controller_";
constexpr std::string_view kBindingKey = "binding_";

const std::vector<PortSubPage> kSubPages = {
    {PortPage::Controls, "port_controls", "Controls"},
    {PortPage::Bindings, "port_bindings", "Key Bindings"},
};

std::vector<PortOptionRow> make_rows() {
  std::vector<PortOptionRow> rows = {
      {"resolution", "Resolution", false, PortPage::Options},
      {"window_mode", "Window Mode", true, PortPage::Options},
      {"shadows", "Dynamic Shadows", false, PortPage::Options},
      {"shadow_quality", "Shadow Quality", false, PortPage::Options},
      {"hud_layout", "HUD Layout", false, PortPage::Options},
      {"hud_vitals", "Health Size", false, PortPage::Options},
      {"hud_potions", "Potion Size", false, PortPage::Options},
      {"hud_portraits", "Portrait Size", false, PortPage::Options},
      {"hud_inset", "Edge Spacing", false, PortPage::Options},
      {"touch_controls", "Touch Controls", false, PortPage::Options},
      {"extraction_revive", "Extraction Revive", false, PortPage::Options},
      {"boot", "Boot", false, PortPage::Options},
      {"controls", "Controls", false, PortPage::Options},
      {"bindings", "Key Bindings", false, PortPage::Options},
  };
  for (unsigned i = 0; i < x2::config::kSettingsKeyboardProfiles; ++i) {
    rows.push_back({std::string(kKeyboardKey) + std::to_string(i + 1u),
                    "Keyboard " + std::to_string(i + 1u), false,
                    PortPage::Controls});
  }
  for (unsigned i = 0; i < kControllerRows; ++i) {
    rows.push_back({std::string(kControllerKey) + std::to_string(i + 1u),
                    "Controller " + std::to_string(i + 1u), false,
                    PortPage::Controls});
  }
  rows.push_back({"profile", "Keyboard", false, PortPage::Bindings});
  rows.push_back(
      {"restore_all", "Restore All Defaults", false, PortPage::Bindings});
  for (unsigned i = 0; i < x2::input::INPUT_BINDING_ROWS; ++i) {
    const char *label = x2::input::input_binding_row_display_label(i);
    rows.push_back({std::string(kBindingKey) + std::to_string(i),
                    label != nullptr ? label : "?", false, PortPage::Bindings});
  }
  return rows;
}

const std::vector<PortOptionRow> kRows = make_rows();

/* The keyboard profile the Key Bindings page shows; not saved. */
std::atomic<unsigned> g_profile{0u};

/* The number after `prefix` in `key`, or -1. */
int numbered(std::string_view key, std::string_view prefix) {
  if (key.substr(0, prefix.size()) != prefix) {
    return -1;
  }
  int value = 0;
  for (const char c : key.substr(prefix.size())) {
    if (c < '0' || c > '9') {
      return -1;
    }
    value = value * 10 + (c - '0');
  }
  return value;
}

std::optional<x2::input::ControllerAssignmentRow> controller(unsigned index) {
  const auto rows =
      x2::input::controller_assignment_rows(*x2::config::settings_store());
  if (index >= rows.size()) {
    return std::nullopt;
  }
  return rows[index];
}

std::string owner_label(int owner) {
  return owner < 0 ? std::string("Off") : "Player " + std::to_string(owner + 1);
}

/* The next seat from `owner` (-1 none) in `direction`, Off between 4 and 1. */
int next_owner(int owner, int direction) {
  const int seats = static_cast<int>(x2::config::kSettingsPlayers) + 1;
  const int from = std::max(owner, -1) + 1;
  return (from + (direction < 0 ? seats - 1 : 1)) % seats - 1;
}

class Queue {
public:
  void push(const PortOptionRow *row, int direction) {
    const std::lock_guard<std::mutex> hold(lock_);
    steps_.emplace_back(row, direction);
  }
  std::deque<std::pair<const PortOptionRow *, int>> take() {
    const std::lock_guard<std::mutex> hold(lock_);
    return std::exchange(steps_, {});
  }

private:
  std::mutex lock_;
  std::deque<std::pair<const PortOptionRow *, int>> steps_;
};

Queue g_queue;

bool save(char *why, int whyn) {
  return x2::config::settings_store_save(why, whyn) != 0;
}

/* A setting with nothing live to apply: change, save, or put it back. */
template <typename Change>
bool stored(Settings *settings, Change &&change, char *why, int whyn) {
  const Settings before = *settings;
  change(*settings);
  if (save(why, whyn)) {
    return true;
  }
  *settings = before;
  return false;
}

bool shadows(Settings *settings, bool quality, int direction, char *why,
             int whyn) {
  const bool saved = stored(
      settings,
      [&](Settings &s) {
        if (!quality) {
          s.dynamic_shadows ^= 1u;
        } else if (direction > 0) {
          s.shadow_resolution =
              s.shadow_resolution >= 4096u
                  ? 512u
                  : static_cast<std::uint16_t>(s.shadow_resolution * 2u);
        } else {
          s.shadow_resolution =
              s.shadow_resolution <= 512u
                  ? 4096u
                  : static_cast<std::uint16_t>(s.shadow_resolution / 2u);
        }
      },
      why, whyn);
  gpu_shadow_configure(settings->dynamic_shadows, settings->shadow_resolution);
  return saved;
}

bool apply(const PortOptionRow &row, int direction, SDL_Window *window,
           char *why, int whyn) {
  Settings *settings = x2::config::settings_store();
  const std::string_view key = row.key;
  if (key == "resolution" || key == "window_mode") {
    const Settings before = *settings;
    if (key == "resolution") {
      x2::presentation::live_resolution_select(settings, window, direction);
    } else {
      settings->window_mode = static_cast<x2::config::WindowMode>(
          cycle(static_cast<unsigned>(settings->window_mode), 3u, direction));
      x2::presentation::render_size_resolve(settings, window);
    }
    return x2::presentation::live_resolution_apply(window, settings, &before,
                                                   why, whyn) != 0;
  }
  if (key == "shadows" || key == "shadow_quality") {
    return shadows(settings, key == "shadow_quality", direction, why, whyn);
  }
  std::string reason;
  if (const int n = numbered(key, kKeyboardKey); n > 0) {
    const unsigned profile = static_cast<unsigned>(n - 1);
    const bool done = x2::input::assign_keyboard(
        *settings, profile,
        next_owner(settings->keyboard_player[profile], direction), &reason);
    std::snprintf(why, static_cast<std::size_t>(whyn), "%s", reason.c_str());
    return done;
  }
  if (const int n = numbered(key, kControllerKey); n > 0) {
    const auto pad = controller(static_cast<unsigned>(n - 1));
    if (!pad) {
      std::snprintf(why, static_cast<std::size_t>(whyn), "no controller");
      return false;
    }
    const bool done = x2::input::assign_controller(
        *settings, *pad, next_owner(pad->owner, direction), &reason);
    std::snprintf(why, static_cast<std::size_t>(whyn), "%s", reason.c_str());
    return done;
  }
  if (key == "profile") {
    g_profile =
        cycle(g_profile, x2::config::kSettingsKeyboardProfiles, direction);
    return true;
  }
  if (key == "restore_all") {
    return stored(
        settings,
        [](Settings &s) {
          x2::config::keyboard_profile_restore_all(
              &s.keyboard_profile[g_profile]);
        },
        why, whyn);
  }
  if (const int n = numbered(key, kBindingKey); n >= 0) {
    return stored(
        settings,
        [n](Settings &s) {
          x2::config::keyboard_profile_restore_row(
              &s.keyboard_profile[g_profile], static_cast<unsigned>(n));
        },
        why, whyn);
  }
  return stored(
      settings,
      [&](Settings &s) {
        X2HudSettings &hud = s.hud;
        if (key == "hud_layout") {
          hud.layout = static_cast<X2HudLayout>(
              cycle(static_cast<unsigned>(hud.layout), 3u, direction));
        } else if (key == "hud_vitals") {
          hud.vitals_scale_percent =
              percent_step(hud.vitals_scale_percent, X2_HUD_SCALE_MIN,
                           X2_HUD_SCALE_MAX, 25u, direction);
        } else if (key == "hud_potions") {
          hud.potions_scale_percent =
              percent_step(hud.potions_scale_percent, X2_HUD_SCALE_MIN,
                           X2_HUD_SCALE_MAX, 25u, direction);
        } else if (key == "hud_portraits") {
          hud.portraits_scale_percent =
              percent_step(hud.portraits_scale_percent, X2_HUD_SCALE_MIN,
                           X2_HUD_SCALE_MAX, 25u, direction);
        } else if (key == "hud_inset") {
          hud.safe_inset_percent = percent_step(
              hud.safe_inset_percent, 0u, X2_HUD_INSET_MAX, 2u, direction);
        } else if (key == "touch_controls") {
          s.touch_controls = static_cast<std::uint8_t>(
              cycle(s.touch_controls, x2::config::kTouchControlsAlways + 1u,
                    direction));
        } else if (key == "extraction_revive") {
          s.extraction_revive = static_cast<x2::config::ExtractionRevive>(
              cycle(static_cast<unsigned>(s.extraction_revive), 3u, direction));
        } else if (key == "boot") {
          s.boot_mode = static_cast<x2::config::BootMode>(
              cycle(static_cast<unsigned>(s.boot_mode), 3u, direction));
        }
      },
      why, whyn);
}

} // namespace

const std::vector<PortSubPage> &port_sub_pages() { return kSubPages; }

std::vector<const PortOptionRow *> port_option_rows() {
  const bool host_is_display = x2::presentation::host_window_fills_display();
  std::vector<const PortOptionRow *> rows;
  for (const PortOptionRow &row : kRows) {
    if (!(row.desktop_only && host_is_display)) {
      rows.push_back(&row);
    }
  }
  return rows;
}

std::vector<const PortOptionRow *> port_option_rows(PortPage page) {
  std::vector<const PortOptionRow *> rows;
  for (const PortOptionRow *row : port_option_rows()) {
    if (row->page == page) {
      rows.push_back(row);
    }
  }
  return rows;
}

const PortOptionRow *port_option_row(std::string_view key) {
  for (const PortOptionRow &row : kRows) {
    if (row.key == key) {
      return &row;
    }
  }
  return nullptr;
}

const PortOptionRow *port_option_row_of_item(std::string_view item) {
  if (item.substr(0, kItemPrefix.size()) != kItemPrefix) {
    return nullptr;
  }
  return port_option_row(item.substr(kItemPrefix.size()));
}

std::string port_option_item(const PortOptionRow &row) {
  return std::string(kItemPrefix) + std::string(row.key);
}

std::string port_option_label(const PortOptionRow &row) {
  if (const int n = numbered(row.key, kControllerKey); n > 0) {
    const auto pad = controller(static_cast<unsigned>(n - 1));
    if (pad && !pad->name.empty()) {
      return pad->name;
    }
  }
  return row.label;
}

std::string port_option_value(const PortOptionRow &row) {
  const Settings &s = *x2::config::settings_store();
  const std::string_view key = row.key;
  if (const int n = numbered(key, kKeyboardKey); n > 0) {
    return owner_label(s.keyboard_player[n - 1]);
  }
  if (const int n = numbered(key, kControllerKey); n > 0) {
    const auto pad = controller(static_cast<unsigned>(n - 1));
    if (!pad) {
      return "None";
    }
    std::string value = owner_label(pad->owner == -2 ? -1 : pad->owner);
    if (pad->name.empty()) {
      value += ", not connected";
    } else if (pad->transient_assignment || !pad->stable_identity) {
      value += ", this session";
    }
    return value;
  }
  if (key == "profile") {
    return "Keyboard " + std::to_string(g_profile + 1u);
  }
  if (key == "controls" || key == "bindings" || key == "restore_all") {
    return {};
  }
  if (const int n = numbered(key, kBindingKey); n >= 0) {
    const auto waiting = x2::input::binding_capture_target();
    if (waiting && waiting->row == static_cast<unsigned>(n)) {
      return "Press a key";
    }
    return x2::input::keyboard_binding_label(s.keyboard_profile[g_profile],
                                             static_cast<unsigned>(n));
  }
  if (key == "resolution") {
    char label[16];
    unsigned width = 0;
    unsigned height = 0;
    x2::presentation::resolution_label(s.render_lines, label, sizeof label);
    x2::presentation::live_resolution_rendered(&s, &width, &height);
    return std::string(label) + " (" + std::to_string(width) + "x" +
           std::to_string(height) + ")";
  }
  if (key == "window_mode") {
    return x2::config::window_mode_label(s.window_mode);
  }
  if (key == "shadows") {
    return s.dynamic_shadows ? "On" : "Off";
  }
  if (key == "shadow_quality") {
    return std::to_string(s.shadow_resolution);
  }
  if (key == "hud_layout") {
    return x2_hud_layout_label(s.hud.layout);
  }
  if (key == "hud_vitals") {
    return percent(s.hud.vitals_scale_percent);
  }
  if (key == "hud_potions") {
    return percent(s.hud.potions_scale_percent);
  }
  if (key == "hud_portraits") {
    return percent(s.hud.portraits_scale_percent);
  }
  if (key == "hud_inset") {
    return percent(s.hud.safe_inset_percent);
  }
  if (key == "touch_controls") {
    return x2::config::touch_controls_label(s.touch_controls);
  }
  if (key == "extraction_revive") {
    return x2::config::extraction_revive_label(s.extraction_revive);
  }
  if (key == "boot") {
    return x2::config::boot_mode_label(s.boot_mode);
  }
  return {};
}

void port_option_choose(const CPU &cpu, const PortOptionRow &row,
                        int direction) {
  for (const PortSubPage &page : kSubPages) {
    if ((row.key == "controls" && page.page == PortPage::Controls) ||
        (row.key == "bindings" && page.page == PortPage::Bindings)) {
      const std::string command = "openmenu " + std::string(page.menu);
      if (!x2::retail::FrontEnd::queue_command(cpu, command)) {
        x2_log_error("PORT OPTIONS: the console refused \"%s\".\n",
                     command.c_str());
      }
      return;
    }
  }
  if (const int n = numbered(row.key, kBindingKey); n >= 0 && direction > 0) {
    x2::input::binding_capture_begin({g_profile, static_cast<unsigned>(n)});
    return;
  }
  g_queue.push(&row, direction);
}

void port_options_service(SDL_Window *window) {
  for (const auto &[row, direction] : g_queue.take()) {
    char why[256] = "";
    if (apply(*row, direction, window, why, static_cast<int>(sizeof why))) {
      x2_log_info("PORT OPTIONS: %s is now %s.\n",
                  port_option_label(*row).c_str(),
                  port_option_value(*row).c_str());
    } else {
      x2_log_error("PORT OPTIONS: %s was not changed: %s.\n",
                   port_option_label(*row).c_str(), why);
    }
  }
}

} // namespace x2::native
