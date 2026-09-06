/* Visual feedback for the title-owned touch layout. Contact acquisition and
 * action publication remain in input/; this document only mirrors the
 * production zones and pressed state into RmlUi. */
#include "touch_document.hpp"

#include "touch_controls.h"
#include "touch_runtime.h"
#include "ui_resources.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace x2::ui {
namespace {

Rml::ElementDocument *document;
Rml::Element *root;
std::vector<X2TouchVisual> visuals;
bool document_visible;
bool last_powers_mode = false;

const char *icon_relative_path(int action, bool powers_active) {
  using x2::input::TouchAction;
  if (powers_active) {
    switch (static_cast<TouchAction>(action)) {
    case TouchAction::LightAttack:
      return "icons/power1.svg";
    case TouchAction::HeavyAttack:
      return "icons/power2.svg";
    case TouchAction::Use:
      return "icons/power3.svg";
    case TouchAction::Jump:
      return "icons/power4.svg";
    default:
      break;
    }
  }
  switch (static_cast<TouchAction>(action)) {
  case TouchAction::LightAttack:
    return "icons/attack.svg";
  case TouchAction::HeavyAttack:
    return "icons/smash.svg";
  case TouchAction::Use:
    return "icons/use.svg";
  case TouchAction::Jump:
    return "icons/jump.svg";
  case TouchAction::Powers:
    return "icons/powers.svg";
  case TouchAction::Pause:
    return "icons/pause.svg";
  default:
    return "";
  }
}

const char *action_title(int action, bool powers_active) {
  using x2::input::TouchAction;
  if (powers_active) {
    switch (static_cast<TouchAction>(action)) {
    case TouchAction::LightAttack:
      return "Power 1";
    case TouchAction::HeavyAttack:
      return "Power 2";
    case TouchAction::Use:
      return "Boost";
    case TouchAction::Jump:
      return "Xtreme";
    default:
      break;
    }
  }
  switch (static_cast<TouchAction>(action)) {
  case TouchAction::LightAttack:
    return "Attack";
  case TouchAction::HeavyAttack:
    return "Smash";
  case TouchAction::Use:
    return "Use";
  case TouchAction::Jump:
    return "Jump";
  case TouchAction::Powers:
    return "Powers";
  case TouchAction::EnergyPack:
    return "Energy";
  case TouchAction::HealthPack:
    return "Health";
  case TouchAction::DecreaseAggr:
    return "Team -";
  case TouchAction::IncreaseAggr:
    return "Team +";
  case TouchAction::MapToggle:
    return "Map";
  case TouchAction::Pause:
    return "Pause";
  case TouchAction::Stats:
    return "Stats";
  default:
    return "";
  }
}

const char *ability_badge_text(int action) {
  using x2::input::TouchAction;
  switch (static_cast<TouchAction>(action)) {
  case TouchAction::LightAttack:
    return "P1";
  case TouchAction::HeavyAttack:
    return "P2";
  case TouchAction::Use:
    return "P3";
  case TouchAction::Jump:
    return "P4";
  default:
    return "";
  }
}

const char *zone_action_class(int action) {
  using x2::input::TouchAction;
  switch (static_cast<TouchAction>(action)) {
  case TouchAction::LightAttack:
    return " zone-light";
  case TouchAction::HeavyAttack:
    return " zone-heavy";
  case TouchAction::Use:
    return " zone-use";
  case TouchAction::Jump:
    return " zone-jump";
  case TouchAction::Powers:
    return " zone-powers";
  case TouchAction::Pause:
    return " zone-pause";
  default:
    return "";
  }
}

const char *badge_css_class(int action) {
  using x2::input::TouchAction;
  switch (static_cast<TouchAction>(action)) {
  case TouchAction::LightAttack:
    return "p1";
  case TouchAction::HeavyAttack:
    return "p2";
  case TouchAction::Use:
    return "p3";
  case TouchAction::Jump:
    return "p4";
  default:
    return "";
  }
}

std::string resource(const std::string &relative) {
  return x2_ui_resource_path(relative.c_str());
}

void rebuild() {
  const size_t count = x2_touch_runtime_visuals(nullptr, 0);
  visuals.resize(count);
  x2_touch_runtime_visuals(visuals.data(), visuals.size());
  std::ostringstream rml;
  for (const auto &visual : visuals) {
    rml << "<div id='touch-zone-" << visual.id << "' class='touch-zone"
        << (visual.stick ? " stick" : zone_action_class(visual.action)) << "'>";
    if (visual.stick) {
      rml << "<div class='touch-stick-knob'></div>";
    } else {
      const char *b_label = ability_badge_text(visual.action);
      if (b_label[0]) {
        rml << "<span id='badge-" << visual.id << "' class='touch-badge "
            << badge_css_class(visual.action) << "'>" << b_label << "</span>";
      }
      const char *icon = icon_relative_path(visual.action, last_powers_mode);
      if (icon[0]) {
        rml << "<img id='icon-" << visual.id << "' class='touch-icon' src='"
            << icon << "' />";
      }
      rml << "<span id='label-" << visual.id << "' class='touch-label'>"
          << action_title(visual.action, last_powers_mode) << "</span>";
    }
    rml << "</div>";
  }
  if (root)
    root->SetInnerRML(rml.str());
}

void set_percent(Rml::Element *element, Rml::PropertyId property, float value) {
  element->SetProperty(property, Rml::Property(value, Rml::Unit::PERCENT));
}

} // namespace

bool touch_document_load(Rml::Context *context) {
  /* The href is RELATIVE and resolved against `base`, exactly as the
     settings document does it. An absolute path here lost its leading
     slash inside RmlUi's path join, so the stylesheet silently failed to
     load and every zone rendered with no style at all -- present in the
     document, invisible on screen. */
  const std::string base = resource("touch_controls.rml");
  const std::string shell =
      "<rml><head><title>Touch Controls</title><link type='text/rcss' "
      "href='touch_controls.rcss' /></head>"
      "<body id='touch-root'></body></rml>";
  document = context->LoadDocumentFromMemory(shell, base);
  if (!document)
    return false;
  root = document->GetElementById("touch-root");
  if (!root)
    return false;
  rebuild();
  document->Hide();
  return true;
}

void touch_document_shutdown() {
  document = nullptr;
  root = nullptr;
  visuals.clear();
  document_visible = false;
  last_powers_mode = false;
}

void touch_document_set_visible(bool visible) {
  if (!document || visible == document_visible)
    return;
  document_visible = visible;
  if (visible)
    document->Show();
  else
    document->Hide();
}

void touch_document_update() {
  if (!document || !document_visible)
    return;
  const size_t count = x2_touch_runtime_visuals(nullptr, 0);
  if (count != visuals.size())
    rebuild();
  if (visuals.empty())
    return;
  x2_touch_runtime_visuals(visuals.data(), visuals.size());

  bool powers_active = false;
  for (const auto &visual : visuals) {
    if (static_cast<x2::input::TouchAction>(visual.action) ==
            x2::input::TouchAction::Powers &&
        visual.active) {
      powers_active = true;
      break;
    }
  }

  if (powers_active != last_powers_mode) {
    last_powers_mode = powers_active;
    if (root)
      root->SetClass("powers-active", powers_active);
    for (const auto &visual : visuals) {
      if (visual.stick)
        continue;
      if (auto *icon_elem =
              document->GetElementById("icon-" + std::to_string(visual.id))) {
        const char *path = icon_relative_path(visual.action, powers_active);
        if (path[0])
          icon_elem->SetAttribute("src", path);
      }
      if (auto *label_elem =
              document->GetElementById("label-" + std::to_string(visual.id))) {
        label_elem->SetInnerRML(action_title(visual.action, powers_active));
      }
    }
  }

  const Rml::Vector2i dimensions = document->GetContext()->GetDimensions();
  if (dimensions.x <= 0 || dimensions.y <= 0)
    return;
  for (const auto &visual : visuals) {
    Rml::Element *element =
        document->GetElementById("touch-zone-" + std::to_string(visual.id));
    if (!element)
      continue;
    const float width = static_cast<float>(dimensions.x);
    const float height = static_cast<float>(dimensions.y);
    set_percent(element, Rml::PropertyId::Left, visual.left * 100.0F / width);
    set_percent(element, Rml::PropertyId::Top, visual.top * 100.0F / height);
    set_percent(element, Rml::PropertyId::Width,
                (visual.right - visual.left) * 100.0F / width);
    set_percent(element, Rml::PropertyId::Height,
                (visual.bottom - visual.top) * 100.0F / height);
    element->SetClass("active", visual.active != 0);
  }
}

} // namespace x2::ui
