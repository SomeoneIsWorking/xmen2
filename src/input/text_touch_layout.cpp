#include "text_touch_layout.hpp"

#include <algorithm>
#include <cstddef>

namespace x2::input {

SceneBox text_button(const SceneBox &text, float finger) {
  const float half_width = 0.5F * (text.width() + kTextButtonGrow * finger);
  const float half_height = 0.5F * kCardHeight * finger;
  const float x = 0.5F * (text.left + text.right);
  const float z = 0.5F * (text.bottom + text.top);
  return {x - half_width, z - half_height, x + half_width, z + half_height};
}

std::vector<SceneBox> footer_buttons(const std::vector<SceneBox> &texts,
                                     float finger) {
  std::vector<SceneBox> out;
  out.reserve(texts.size());
  for (const SceneBox &text : texts) {
    out.push_back(text_button(text, finger));
  }
  const float gap = kFooterGap * finger;
  std::vector<bool> placed(texts.size(), false);
  for (std::size_t first = 0; first < texts.size(); ++first) {
    if (placed[first]) {
      continue;
    }
    std::vector<std::size_t> row;
    for (std::size_t i = first; i < texts.size(); ++i) {
      if (!placed[i] && texts[i].bottom < texts[first].top &&
          texts[first].bottom < texts[i].top) {
        row.push_back(i);
        placed[i] = true;
      }
    }
    std::sort(row.begin(), row.end(), [&](std::size_t a, std::size_t b) {
      return out[a].left + out[a].right < out[b].left + out[b].right;
    });
    bool crowded = false;
    for (std::size_t i = 1; i < row.size(); ++i) {
      crowded = crowded || out[row[i]].left < out[row[i - 1]].right + gap;
    }
    if (!crowded) {
      continue;
    }
    float left = out[row.front()].left;
    float right = out[row.front()].right;
    for (const std::size_t i : row) {
      left = std::min(left, out[i].left);
      right = std::max(right, out[i].right);
    }
    const auto count = static_cast<float>(row.size());
    const float width = (right - left - gap * (count - 1.0F)) / count;
    for (std::size_t k = 0; k < row.size(); ++k) {
      SceneBox &button = out[row[k]];
      button.left = left + static_cast<float>(k) * (width + gap);
      button.right = button.left + width;
    }
  }
  return out;
}

SceneBox row_button(const SceneBox &text, float left, float right,
                    float finger) {
  const float half_height = 0.5F * kCardHeight * finger;
  const float z = 0.5F * (text.bottom + text.top);
  return {left, z - half_height, right, z + half_height};
}

} // namespace x2::input
