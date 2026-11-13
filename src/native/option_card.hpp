#ifndef X2_OPTION_CARD_HPP
#define X2_OPTION_CARD_HPP

#include "menu_scene.hpp"

#include <array>
#include <cstdint>

/* One card of the game's PDA option row, every layer the PDA draws for a
   row: the frame, the focus or no-focus panel, the light at its right end
   and, beside a rail, the side hinge, each placed on the frame as the PDA's
   scene places option01's layers (external/xmen2-decomp/docs/notes/menus.md). */
namespace x2::native {

class OptionCard {
public:
  /* Loads the layers; `hinge` adds the side hinge left of the frame. */
  void load(guest::GuestCall &call, menu_scene::Vectors &vectors, bool hinge);
  void release(guest::GuestCall &call);

  /* The card with its frame over `frame` and its nearest face at `depth`. */
  void show(guest::GuestCall &call, menu_scene::Vectors &vectors,
            const input::SceneBox &frame, float depth, bool focused);
  void hide(guest::GuestCall &call);

private:
  enum Layer : std::uint8_t {
    kFrame,
    kFocus,
    kNoFocus,
    kLightOn,
    kLightOff,
    kHinge,
    kLayers
  };
  struct Model {
    std::uint32_t entity = 0u;
    menu_scene::Art art;
    bool attached = false;
  };
  void put(guest::GuestCall &call, menu_scene::Vectors &vectors, Layer layer,
           presentation::ScenePoint origin, float depth, float sx, float sz);

  std::array<Model, kLayers> models_{};
};

} // namespace x2::native

#endif
