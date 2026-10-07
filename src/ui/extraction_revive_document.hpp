#ifndef X2_EXTRACTION_REVIVE_DOCUMENT_HPP
#define X2_EXTRACTION_REVIVE_DOCUMENT_HPP

namespace Rml {
class Context;
class ElementDocument;
class Element;
} // namespace Rml

namespace x2::ui {

/* The paid revive prompt drawn at kRevivePromptRect. */
class ExtractionReviveDocument {
public:
  bool load(Rml::Context *context);
  void shutdown();
  /* An offer or an answer to draw this frame. */
  static bool wanted();
  /* Show, place and word it from the prompt, or hide it. */
  void update();

private:
  Rml::ElementDocument *document_ = nullptr;
  Rml::Element *button_ = nullptr;
  Rml::Element *label_ = nullptr;
  Rml::Element *hint_ = nullptr;
  bool visible_ = false;
};

} // namespace x2::ui

#endif
