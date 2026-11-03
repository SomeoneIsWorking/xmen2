#include "port_option_rows.hpp"

#include "display_geometry.h"
#include "gpu_shadow.h"
#include "live_resolution.h"
#include "render_size.h"
#include "resolution_ladder.h"
#include "settings_store.h"
#include "x2_log.h"

#include <cstdio>
#include <deque>
#include <mutex>
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

const std::vector<PortOptionRow> kRows = {
    {"resolution", "Resolution", false},
    {"window_mode", "Window Mode", true},
    {"shadows", "Dynamic Shadows", false},
    {"shadow_quality", "Shadow Quality", false},
    {"hud_layout", "HUD Layout", false},
    {"hud_vitals", "Health Size", false},
    {"hud_potions", "Potion Size", false},
    {"hud_portraits", "Portrait Size", false},
    {"hud_inset", "Edge Spacing", false},
    {"touch_controls", "Touch Controls", false},
    {"extraction_revive", "Extraction Revive", false},
    {"boot", "Boot", false},
};

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

std::string port_option_value(const PortOptionRow &row) {
  const Settings &s = *x2::config::settings_store();
  const std::string_view key = row.key;
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

void port_option_request(const PortOptionRow &row, int direction) {
  g_queue.push(&row, direction);
}

void port_options_service(SDL_Window *window) {
  for (const auto &[row, direction] : g_queue.take()) {
    char why[256] = "";
    if (apply(*row, direction, window, why, static_cast<int>(sizeof why))) {
      x2_log_info("PORT OPTIONS: %.*s is now %s.\n",
                  static_cast<int>(row->label.size()), row->label.data(),
                  port_option_value(*row).c_str());
    } else {
      x2_log_error("PORT OPTIONS: %.*s was not changed: %s.\n",
                   static_cast<int>(row->label.size()), row->label.data(), why);
    }
  }
}

} // namespace x2::native
