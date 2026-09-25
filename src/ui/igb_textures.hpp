/*
 * Game-owned art for the RmlUi overlay: an IGB texture from the user's own
 * install, decoded by shared/alchemy's reader and handed to RmlUi as an
 * ordinary image source.
 *
 * A document names one cell of such an image as `igb:<key>#<cell>@<C>x<R>`,
 * the cell counted in a C-column, R-row grid. The key
 * is issued here for a host path, so no filesystem path is ever written into
 * markup, and the render interface is the only place that turns one back into
 * pixels.
 *
 * The images this serves are the heroes' power icon atlases
 * (Textures/ui/<hero>_icons1.IGB): a 4x4 grid of round icons, where a move's
 * `icon="N"` names cell N in reading order. Alchemy stores their rows bottom
 * first, so the decoded rows are reversed before the cell is cut -- measured
 * against the game's own RT ring, which draws Magneto's innate power (icon 4)
 * from what is the third stored row, upside down unless reversed.
 */
#pragma once

#include <RmlUi/Core.h>
#include <RmlUi_Renderer_SDL_GPU.h>

#include <map>
#include <string>

namespace x2::ui {

inline constexpr const char *kIgbSourcePrefix = "igb:";
inline constexpr int kIconAtlasColumns = 4;
inline constexpr int kIconAtlasRows = 4;
inline constexpr int kHudAtlasRows = 8;

/* How an atlas is divided: the power and talent atlases are 4x4 grids of
   round icons; Textures/ui/hud.IGB is a 4x8 grid of 32-pixel cells. */
struct IconGrid {
  int columns = kIconAtlasColumns;
  int rows = kIconAtlasRows;
};

class IgbTextureRenderInterface final : public RenderInterface_SDL_GPU {
public:
  using RenderInterface_SDL_GPU::RenderInterface_SDL_GPU;

  /* The source for one icon cell of an IGB atlas on the host; the same path
     always gets the same key, so a rebuilt document does not reload it. */
  std::string source_for(const std::string &host_path, int cell,
                         IconGrid grid = {});

  Rml::TextureHandle LoadTexture(Rml::Vector2i &texture_dimensions,
                                 const Rml::String &source) override;

private:
  std::map<std::string, std::string> keys_;  // host path -> key
  std::map<std::string, std::string> paths_; // key -> host path
};

/* The live overlay's interface, or null before RmlUi is up. */
IgbTextureRenderInterface *igb_texture_interface();

} // namespace x2::ui
