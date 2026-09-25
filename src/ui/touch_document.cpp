/* Visual feedback for the title-owned touch layout. Contact acquisition and
 * action publication remain in input/; this document only mirrors the
 * production zones and pressed state into RmlUi. */
#include "touch_document.hpp"

#include "igb_textures.hpp"
#include "power_slots.h"
#include "touch_art.h"
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
/* The image each button shows, so a hero swap that keeps the same number
   of powers still redraws them. */
std::string icon_sources;

/* One cell of a retail art file. */
struct GameIcon {
  X2TouchArt art;
  int cell;
  IconGrid grid;
};

/* The game's own round icon for an action button, in the same style as the
   power icons beside it: the talent atlas's fist, double fist, open hand and
   wing, and the HUD atlas's screen frame for the port menu. False for an
   action drawn by the retail HUD itself or by nothing. */
bool game_icon(int action, GameIcon &icon) {
  using x2::input::TouchAction;
  switch (static_cast<TouchAction>(action)) {
  case TouchAction::LightAttack:
    icon = {X2_TOUCH_ART_TALENTS, 6, {}};
    return true;
  case TouchAction::HeavyAttack:
    icon = {X2_TOUCH_ART_TALENTS, 2, {}};
    return true;
  case TouchAction::Use:
    icon = {X2_TOUCH_ART_TALENTS, 3, {}};
    return true;
  case TouchAction::Jump:
    icon = {X2_TOUCH_ART_TALENTS, 7, {}};
    return true;
  case TouchAction::PortMenu:
    icon = {X2_TOUCH_ART_HUD, 4, {kIconAtlasColumns, kHudAtlasRows}};
    return true;
  default:
    return false;
  }
}

/* True for a control laid over art the game draws itself -- a hero
   portrait, a potion or a mouse-overlay menu icon -- which gets a ring and
   no fill of its own. */
bool over_retail_art(int action) {
  using x2::input::TouchAction;
  switch (static_cast<TouchAction>(action)) {
  case TouchAction::SelectHero1:
  case TouchAction::SelectHero2:
  case TouchAction::SelectHero3:
  case TouchAction::SelectHero4:
  case TouchAction::HealthPack:
  case TouchAction::EnergyPack:
  case TouchAction::RetailPauseMenu:
  case TouchAction::RetailTeamMenu:
    return true;
  default:
    return false;
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

/* The kind decides the element's style, and a prompt has no action of its
   own: it is the retail UI's own word with a control drawn round it. */
const char *visual_class(const X2TouchVisual &visual) {
  switch (visual.kind) {
  case X2_TOUCH_VISUAL_STICK:
    return " stick";
  case X2_TOUCH_VISUAL_PROMPT:
    return " prompt";
  default:
    if (over_retail_art(visual.action)) {
      return " zone-ring";
    }
    return " zone-icon";
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

/* The game's icon for an action button; empty when it has none or the
   art is not available. */
std::string action_source(const X2TouchVisual &visual) {
  IgbTextureRenderInterface *textures = igb_texture_interface();
  GameIcon icon{};
  if (!textures || !game_icon(visual.action, icon)) {
    return {};
  }
  const char *path = x2_touch_art_path(icon.art);
  if (!path[0]) {
    return {};
  }
  return textures->source_for(path, icon.cell, icon.grid);
}

std::string icon_source(const X2TouchVisual &visual) {
  return visual.power_icon >= 0 ? power_source(visual) : action_source(visual);
}

std::string all_icon_sources() {
  std::string all;
  for (const auto &visual : visuals) {
    all += icon_source(visual) + ";";
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
  icon_sources = all_icon_sources();
  std::ostringstream rml;
  for (const auto &visual : visuals) {
    rml << "<div id='touch-zone-" << visual.id << "' class='touch-zone"
        << visual_class(visual) << "'>";
    if (visual.kind == X2_TOUCH_VISUAL_STICK) {
      rml << "<div class='touch-stick-knob'></div>";
    } else if (visual.kind != X2_TOUCH_VISUAL_PROMPT) {
      const std::string source = icon_source(visual);
      if (!source.empty()) {
        rml << "<img class='touch-game-icon' src='" << source << "' />";
      } else {
        const char *title = action_title(visual.action);
        if (title[0]) {
          rml << "<span class='touch-label'>" << title << "</span>";
        }
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
  icon_sources.clear();
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
  if (all_icon_sources() != icon_sources)
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
