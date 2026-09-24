/* Visual feedback for the title-owned touch layout. Contact acquisition and
 * action publication remain in input/; this document only mirrors the
 * production zones and pressed state into RmlUi. */
#include "touch_document.hpp"

#include "igb_textures.hpp"
#include "power_slots.h"
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
/* The image each power button shows, so a hero swap that keeps the same
   number of powers still redraws them. */
std::string power_sources;

/* The game's own prompt glyph for the pad button a control presses -- the
   same shared/port-assets art the retail prompts are lettered in -- drawn
   inside the port's circle. */
const char *glyph_relative_path(int action) {
  using x2::input::TouchAction;
  switch (static_cast<TouchAction>(action)) {
  case TouchAction::LightAttack:
    return "touch/face_a.svg";
  case TouchAction::HeavyAttack:
    return "touch/face_b.svg";
  case TouchAction::Use:
    return "touch/face_x.svg";
  case TouchAction::Jump:
    return "touch/face_y.svg";
  case TouchAction::Pause:
    return "touch/start.svg";
  default:
    return "";
  }
}

const char *action_title(int action) {
  using x2::input::TouchAction;
  switch (static_cast<TouchAction>(action)) {
  case TouchAction::LightAttack:
    return "Attack";
  case TouchAction::HeavyAttack:
    return "Smash";
  case TouchAction::Use:
    return "Use";
  case TouchAction::Jump:
    return "Jump";
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
  case TouchAction::Pause:
    return " zone-pause";
  default:
    return "";
  }
}

/* The kind decides the element's style, and a prompt has no action of its
   own: it is the retail UI's own word with a control drawn round it. */
const char *visual_class(const X2TouchVisual &visual) {
  switch (visual.kind) {
  case X2_TOUCH_VISUAL_STICK:
    return " stick";
  case X2_TOUCH_VISUAL_PROMPT:
    return " prompt";
  default:
    return visual.power_icon >= 0 ? " zone-power"
                                  : zone_action_class(visual.action);
  }
}

/* The game's icon for a power button, from the hero's own atlas. */
std::string power_source(const X2TouchVisual &visual) {
  IgbTextureRenderInterface *textures = igb_texture_interface();
  const char *atlas = x2_power_slots_atlas();
  if (visual.power_icon < 0 || !textures || !atlas[0]) {
    return {};
  }
  return textures->source_for(atlas, visual.power_icon);
}

std::string all_power_sources() {
  std::string all;
  for (const auto &visual : visuals) {
    all += power_source(visual) + ";";
  }
  return all;
}

std::string resource(const std::string &relative) {
  return x2_ui_resource_path(relative.c_str());
}

void rebuild() {
  const size_t count = x2_touch_runtime_visuals(nullptr, 0);
  visuals.resize(count);
  x2_touch_runtime_visuals(visuals.data(), visuals.size());
  power_sources = all_power_sources();
  std::ostringstream rml;
  for (const auto &visual : visuals) {
    rml << "<div id='touch-zone-" << visual.id << "' class='touch-zone"
        << visual_class(visual) << "'>";
    if (visual.kind == X2_TOUCH_VISUAL_STICK) {
      rml << "<div class='touch-stick-knob'></div>";
    } else if (visual.power_icon >= 0) {
      const std::string source = power_source(visual);
      if (!source.empty()) {
        rml << "<img class='touch-power-icon' src='" << source << "' />";
      }
    } else if (visual.kind != X2_TOUCH_VISUAL_PROMPT) {
      const char *glyph = glyph_relative_path(visual.action);
      if (glyph[0]) {
        rml << "<img class='touch-icon' src='" << glyph << "' />";
      }
      const char *title = action_title(visual.action);
      if (title[0]) {
        rml << "<span class='touch-label'>" << title << "</span>";
      }
    }
    rml << "</div>";
  }
  if (root)
    root->SetInnerRML(rml.str());
}

void set_percent(Rml::Element *element, Rml::PropertyId property, float value) {
  element->SetProperty(property, Rml::Property(value, Rml::Unit::PERCENT));
}

/*
 * The knob sits where the thumb has pushed the stick.
 *
 * It used to be drawn dead centre whatever the player did, so the one
 * control with a continuous value was the only one that never showed its
 * value: a thumb sliding to a stop against the dead zone and a thumb at
 * full deflection looked identical. The knob's travel is the ring's own
 * radius less its size, so full deflection puts it against the inside of
 * the ring rather than outside it.
 */
void place_knob(Rml::Element *zone, const X2TouchVisual &visual) {
  Rml::Element *knob = zone->GetChild(0);
  if (!knob) {
    return;
  }
  /* The stylesheet owns how big the knob is; this reads that size back
     rather than keeping a second copy of it, so restyling the ring cannot
     silently move the knob's travel out of it. */
  const Rml::Vector2f ring = zone->GetBox().GetSize();
  const Rml::Vector2f size = knob->GetBox().GetSize();
  if (ring.x <= 0.0F || ring.y <= 0.0F) {
    return;
  }
  const float span_x = (ring.x - size.x) * 0.5F;
  const float span_y = (ring.y - size.y) * 0.5F;
  set_percent(knob, Rml::PropertyId::Left,
              (span_x + visual.deflect_x * span_x) * 100.0F / ring.x);
  set_percent(knob, Rml::PropertyId::Top,
              (span_y + visual.deflect_y * span_y) * 100.0F / ring.y);
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
  power_sources.clear();
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
  if (all_power_sources() != power_sources)
    rebuild();

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
    if (visual.kind == X2_TOUCH_VISUAL_STICK) {
      place_knob(element, visual);
    }
  }
}

} // namespace x2::ui
