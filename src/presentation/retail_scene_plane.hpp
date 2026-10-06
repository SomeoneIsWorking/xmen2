#ifndef X2_RETAIL_SCENE_PLANE_HPP
#define X2_RETAIL_SCENE_PLANE_HPP

#include <optional>

namespace x2::presentation {

/* A point in the retail scene plane; z increases upward. */
struct ScenePoint {
  float x = 0.0F;
  float z = 0.0F;
};

/* A point in the guest's client area, the logical backbuffer. */
struct ClientPoint {
  int x = 0;
  int y = 0;
};

/*
 * The plane the retail menu items' hit boxes live in, and the mapping the
 * retail mouse handler FUN_005f9eb0 applies to a client point to reach it
 * (docs/RE/menus.md): integer arithmetic against the logical backbuffer at
 * DAT_00a09ffc/DAT_00a0a000.
 */
class RetailScenePlane {
public:
  /* The viewport singleton's aspect and scales (docs/RE/hud.md) and the
     logical backbuffer; nullopt when any of them describe no plane. */
  static std::optional<RetailScenePlane>
  from_viewport(float aspect, float scale_x, float scale_z, unsigned client_w,
                unsigned client_h);

  /* FUN_005f9eb0's own mapping. */
  ScenePoint to_scene(ClientPoint client) const;
  /* The client point that mapping takes to `scene`, clamped to the client
     area. */
  ClientPoint to_client(ScenePoint scene) const;

  float left() const { return left_; }
  float width() const { return width_; }
  float height() const { return height_; }

private:
  float left_ = 0.0F;
  float width_ = 0.0F;
  float height_ = 0.0F;
  unsigned client_w_ = 0u;
  unsigned client_h_ = 0u;
};

} // namespace x2::presentation

#endif
