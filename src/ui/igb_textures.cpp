#include "igb_textures.hpp"

#include "host_files.h"
#include "x2_log.h"

// igb is a C library whose header declares no C++ linkage.
extern "C" {
#include <igb.h>
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace x2::ui {
namespace {

using Rgba = std::unique_ptr<uint8_t, decltype(&std::free)>;

/* Clear what lies outside the cell's inscribed circle. The atlas corners are
   opaque black -- the game hides them under its ring's frame -- and a touch
   button is that circle, so the icon is cut to it with a one-pixel soft edge.
   The result is premultiplied, as the overlay renderer blends and as its own
   file loader converts every other image. */
void cut_to_circle_premultiplied(std::vector<Rml::byte> &rgba,
                                 Rml::Vector2i size) {
  const float radius = static_cast<float>(std::min(size.x, size.y)) * 0.5F;
  for (int y = 0; y < size.y; ++y) {
    for (int x = 0; x < size.x; ++x) {
      const float dx = static_cast<float>(x) + 0.5F - size.x * 0.5F;
      const float dy = static_cast<float>(y) + 0.5F - size.y * 0.5F;
      const float coverage =
          std::clamp(radius - std::sqrt(dx * dx + dy * dy), 0.0F, 1.0F);
      Rml::byte *pixel = &rgba[static_cast<size_t>((y * size.x + x) * 4)];
      const float alpha = static_cast<float>(pixel[3]) / 255.0F * coverage;
      for (int channel = 0; channel < 3; ++channel) {
        pixel[channel] = static_cast<Rml::byte>(
            std::lround(static_cast<float>(pixel[channel]) * alpha));
      }
      pixel[3] = static_cast<Rml::byte>(std::lround(255.0F * alpha));
    }
  }
}

/* One cell of a bottom-first atlas, top row first. Empty when the image is
   not the grid the atlases are. */
std::vector<Rml::byte> atlas_cell(const uint8_t *rgba, int width, int height,
                                  int cell, Rml::Vector2i &size) {
  if (width % kIconAtlasColumns || height % kIconAtlasRows || cell < 0 ||
      cell >= kIconAtlasColumns * kIconAtlasRows) {
    return {};
  }
  size = {width / kIconAtlasColumns, height / kIconAtlasRows};
  const int left = (cell % kIconAtlasColumns) * size.x;
  const int top = (cell / kIconAtlasColumns) * size.y;
  std::vector<Rml::byte> out(static_cast<size_t>(size.x * size.y * 4));
  for (int row = 0; row < size.y; ++row) {
    const int stored = height - 1 - (top + row);
    std::memcpy(&out[static_cast<size_t>(row * size.x * 4)],
                rgba + (static_cast<size_t>(stored) * width + left) * 4,
                static_cast<size_t>(size.x) * 4);
  }
  cut_to_circle_premultiplied(out, size);
  return out;
}

/* Opens `path` through the host file layer, so an install in an Android
   document tree reads like any other. */
int open_igb(igb &file, const char *path) {
  FILE *stream = x2::native::host_fopen(path, "rb");
  if (!stream) {
    return -1;
  }
  long size = -1;
  if (std::fseek(stream, 0, SEEK_END) == 0) {
    size = std::ftell(stream);
  }
  auto *bytes =
      size > 0 ? static_cast<uint8_t *>(std::malloc(static_cast<size_t>(size)))
               : nullptr;
  const bool read = bytes && std::fseek(stream, 0, SEEK_SET) == 0 &&
                    std::fread(bytes, 1, static_cast<size_t>(size), stream) ==
                        static_cast<size_t>(size);
  std::fclose(stream);
  if (!read) {
    std::free(bytes);
    return -1;
  }
  return igb_open_bytes(&file, bytes, static_cast<size_t>(size));
}

} // namespace

std::string IgbTextureRenderInterface::source_for(const std::string &host_path,
                                                  int cell) {
  auto found = keys_.find(host_path);
  if (found == keys_.end()) {
    std::string key = std::to_string(keys_.size());
    paths_.emplace(key, host_path);
    found = keys_.emplace(host_path, key).first;
  }
  return kIgbSourcePrefix + found->second + "#" + std::to_string(cell);
}

Rml::TextureHandle
IgbTextureRenderInterface::LoadTexture(Rml::Vector2i &texture_dimensions,
                                       const Rml::String &source) {
  if (source.rfind(kIgbSourcePrefix, 0) != 0) {
    return RenderInterface_SDL_GPU::LoadTexture(texture_dimensions, source);
  }
  const size_t hash = source.find('#');
  const auto path = paths_.find(source.substr(
      std::strlen(kIgbSourcePrefix), hash - std::strlen(kIgbSourcePrefix)));
  if (hash == Rml::String::npos || path == paths_.end()) {
    x2_log_error("igb texture: %s was never issued\n", source.c_str());
    return {};
  }
  const int cell = std::atoi(source.c_str() + hash + 1);
  igb file{};
  if (open_igb(file, path->second.c_str()) != 0) {
    x2_log_error("igb texture: cannot read %s\n", path->second.c_str());
    return {};
  }
  igb_image image{};
  Rml::TextureHandle handle{};
  if (igb_find_images(&file, &image, 1) < 1) {
    x2_log_error("igb texture: %s holds no image\n", path->second.c_str());
  } else {
    int length = 0;
    const Rgba rgba(igb_image_to_rgba(&image, &length), &std::free);
    Rml::Vector2i size;
    const auto pixels =
        rgba && length == image.width * image.height * 4
            ? atlas_cell(rgba.get(), image.width, image.height, cell, size)
            : std::vector<Rml::byte>{};
    if (pixels.empty()) {
      x2_log_error("igb texture: %s image %dx%d format %d has no cell %d\n",
                   path->second.c_str(), image.width, image.height,
                   image.pixel_format, cell);
    } else {
      texture_dimensions = size;
      handle = GenerateTexture({pixels.data(), pixels.size()}, size);
    }
  }
  igb_close(&file);
  return handle;
}

} // namespace x2::ui
