#ifndef X2_TOUCH_DOCUMENT_HPP
#define X2_TOUCH_DOCUMENT_HPP

namespace Rml {
class Context;
}

namespace x2::ui {

bool touch_document_load(Rml::Context *context);
void touch_document_shutdown();
void touch_document_set_visible(bool visible);
void touch_document_update();

} // namespace x2::ui

#endif /* X2_TOUCH_DOCUMENT_HPP */
