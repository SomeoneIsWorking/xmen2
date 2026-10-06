#ifndef X2_SKIP_DOCUMENT_HPP
#define X2_SKIP_DOCUMENT_HPP

namespace Rml {
class Context;
class ElementDocument;
class Element;
} // namespace Rml

namespace x2::ui {

/*
 * The cinematic's Skip button, drawn.
 *
 * Its own document rather than a zone of the touch overlay: that overlay is
 * hidden while a cinematic holds the controls, which is the only time this
 * button exists. Placement, the offer and the press all belong to input/
 * (touch_skip_button.h); this mirrors x2_touch_runtime_skip_button and
 * nothing else. Styled inline in the touch overlay's ring-and-fill look, so it
 * reads as one of the same set of controls.
 */
class SkipDocument {
public:
  bool load(Rml::Context *context);
  void shutdown();
  /* Is there a Skip button to draw this frame? Asked before rendering, so a
     frame with nothing else to draw still draws it. */
  static bool wanted();
  /* Show, place and highlight it from the runtime, or hide it. */
  void update();

private:
  Rml::ElementDocument *document_ = nullptr;
  Rml::Element *button_ = nullptr;
  bool visible_ = false;
};

} // namespace x2::ui

#endif /* X2_SKIP_DOCUMENT_HPP */
