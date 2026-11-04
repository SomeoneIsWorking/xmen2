#include "option_card.hpp"

#include <string_view>

namespace x2::native {
namespace {

/* A layer's model and where the PDA's scene puts its origin beside
   option01's frame origin, in art units at the frame's scale (pda.xmlb's
   option01, option01_focus, option01_light and pause_bracket_01: GET /menu
   boxes less each model's own bounds; the panel is centred as the PDA draws
   it). */
struct LayerArt {
  std::string_view model;
  float across;
  float up;
};

constexpr std::array<LayerArt, 6> kLayerArt = {{
    {"ui/models/m_pda_option_screen", 0.0F, 0.0F},
    {"ui/models/m_pda_option_focus", -5.0F, 0.0F},
    {"ui/models/m_pda_option_nofocus", -5.0F, 0.0F},
    {"ui/models/m_pda_option_light_on", 70.0F, 4.0F},
    {"ui/models/m_pda_option_light_off", 70.0F, 4.0F},
    {"ui/models/m_pda_side_hinge", -105.0F, 12.0F},
}};

/* Art is shown at the end of its play-in; the layers over the frame sit this
   much in front of it. */
constexpr float kSettled = 0.999F;
constexpr float kOverFrame = 12.0F;

} // namespace

void OptionCard::load(guest::GuestCall &call, menu_scene::Vectors &vectors,
                      bool hinge) {
  for (std::size_t i = 0; i < models_.size(); ++i) {
    if (i == kHinge && !hinge) {
      continue;
    }
    Model &model = models_[i];
    model.entity = menu_scene::load_model(call, kLayerArt[i].model);
    model.art = menu_scene::bounds(call, vectors, model.entity);
    menu_scene::stretch(call, vectors, model.entity, {}, {}, 0.0F);
  }
}

void OptionCard::release(guest::GuestCall &call) {
  hide(call);
  for (Model &model : models_) {
    menu_scene::release(call, model.entity);
    model = {};
  }
}

void OptionCard::show(guest::GuestCall &call, menu_scene::Vectors &vectors,
                      const input::SceneBox &frame, float depth, bool focused) {
  hide(call);
  const menu_scene::Art &art = models_[kFrame].art;
  if (models_[kFrame].entity == 0u || !(art.box.width() > 0.0F) ||
      !(art.box.height() > 0.0F)) {
    return;
  }
  const float sx = frame.width() / art.box.width();
  const float sz = frame.height() / art.box.height();
  const presentation::ScenePoint origin{frame.left - art.box.left * sx,
                                        frame.top - art.box.top * sz};
  const float back = depth - art.front;
  put(call, vectors, kFrame, origin, back, sx, sz);
  put(call, vectors, focused ? kFocus : kNoFocus, origin, back - kOverFrame, sx,
      sz);
  put(call, vectors, focused ? kLightOn : kLightOff, origin, back - kOverFrame,
      sx, sz);
  put(call, vectors, kHinge, origin, back - kOverFrame, sx, sz);
}

void OptionCard::hide(guest::GuestCall &call) {
  for (Model &model : models_) {
    if (model.attached) {
      menu_scene::detach(call, model.entity);
      model.attached = false;
    }
  }
}

void OptionCard::put(guest::GuestCall &call, menu_scene::Vectors &vectors,
                     Layer layer, presentation::ScenePoint origin, float depth,
                     float sx, float sz) {
  Model &model = models_[layer];
  if (model.entity == 0u) {
    return;
  }
  const LayerArt &art = kLayerArt[layer];
  menu_scene::place(call, vectors, model.entity,
                    {origin.x + art.across * sx, origin.z + art.up * sz}, depth,
                    sx, sz);
  menu_scene::show_phase(call, model.entity, kSettled);
  menu_scene::attach(call, model.entity);
  model.attached = true;
}

} // namespace x2::native
