#ifndef X2_AUTOSAVE_NOTICE_DOCUMENT_HPP
#define X2_AUTOSAVE_NOTICE_DOCUMENT_HPP

namespace Rml {
class Context;
class ElementDocument;
class Element;
} // namespace Rml

namespace x2::ui {

/* The autosave notice, a small pill at the top centre, clear of the touch
 * layout and the revive prompt. */
class AutosaveNoticeDocument {
public:
  bool load(Rml::Context *context);
  void shutdown();
  static bool wanted();
  void update();

private:
  Rml::ElementDocument *document_ = nullptr;
  Rml::Element *label_ = nullptr;
  bool visible_ = false;
};

} // namespace x2::ui

#endif
