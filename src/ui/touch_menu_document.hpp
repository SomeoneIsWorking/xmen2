#ifndef X2_TOUCH_MENU_DOCUMENT_HPP
#define X2_TOUCH_MENU_DOCUMENT_HPP

#include <string>

namespace Rml {
class Context;
class ElementDocument;
class Element;
} // namespace Rml

namespace x2::ui {

/*
 * The touch menu, drawn: an opaque full-screen document over a retail menu the
 * touch menu replaces. It mirrors x2::input::touch_menu_state() and nothing
 * else; the layout, the contacts and what a tap does belong to input/
 * (touch_menu.hpp).
 */
class TouchMenuDocument {
public:
  bool load(Rml::Context *context);
  void shutdown();
  /* Is the touch menu shown this frame? Asked before rendering, so a frame
     with only this to draw still draws it. */
  static bool wanted();
  /* False while something modal covers it. */
  void set_visible(bool allowed) { allowed_ = allowed; }
  void update();

private:
  Rml::ElementDocument *document_ = nullptr;
  Rml::Element *root_ = nullptr;
  std::string drawn_;
  bool allowed_ = false;
  bool visible_ = false;
};

} // namespace x2::ui

#endif
