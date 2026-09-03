#ifndef X2_PROMPT_LABELS_H
#define X2_PROMPT_LABELS_H

#include <stddef.h>
#include <stdint.h>

enum PromptLabelStyle {
  PROMPT_LABEL_UNCHANGED = 0,
  PROMPT_LABEL_PAD_GLYPH,
  PROMPT_LABEL_KEYCAP
};

/* Restyle one complete game label. Returns unchanged when output is too small.
 */
enum PromptLabelStyle prompt_label_rewrite(const uint8_t *input,
                                           uint8_t *output, size_t capacity);
void prompt_labels_report(void);

#endif /* X2_PROMPT_LABELS_H */
