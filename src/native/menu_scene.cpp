#include "menu_scene.hpp"

#include "guest_modules.h"

#include <cstring>

namespace x2::native::menu_scene {
namespace {

/* CMenuMgr, its scene's attach and detach (entity, 0), and its model
   loader (&entity, name, 0xd) as FUN_005c10d0 calls it. */
constexpr std::uint32_t kMenuManager = 0x005d8920u;
constexpr std::uint32_t kAttach = 0x108u;
constexpr std::uint32_t kDetach = 0x10cu;
constexpr std::uint32_t kLoadModel = 0x260u;
constexpr std::uint32_t kLoadKind = 0xdu;
/* The loaded entity's start (0), and FUN_00575980(&entity), the release. */
constexpr std::uint32_t kEntityStart = 0x40u;
constexpr std::uint32_t kRelease = 0x00575980u;
/* An entity's bounds (min, max) and transform; the transform's vt+0x14
   (pos, rot, sx, sdepth, sz). */
constexpr std::uint32_t kEntityBounds = 0x28u;
constexpr std::uint32_t kEntityTransform = 0x8u;
constexpr std::uint32_t kPlaceScaled = 0x14u;
/* An entity's animation holder (vt+0x10), its animation (vt+0x14) and the
   shown phase (vt+0x30). */
constexpr std::uint32_t kEntityAnimation = 0x10u;
constexpr std::uint32_t kAnimation = 0x14u;
constexpr std::uint32_t kAnimationPhase = 0x30u;
/* The style colour (selected, out, enabled, item, custom) and FUN_0040be00,
   the vec4 to packed colour. */
constexpr std::uint32_t kStyleColor = 0x005d8ae0u;
constexpr std::uint32_t kPackColor = 0x0040be00u;

std::uint32_t manager(guest::GuestCall &call) {
  return call.cdecl_call(exe_linked(kMenuManager));
}

} // namespace

std::uint32_t Vectors::put(int slot, float a, float b, float c, float d) {
  const std::uint32_t at =
      block_.address() + static_cast<std::uint32_t>(slot) * 16u;
  WRF32(at, a);
  WRF32(at + 4u, b);
  WRF32(at + 8u, c);
  WRF32(at + 12u, d);
  return at;
}

std::uint32_t load_model(guest::GuestCall &call, std::string_view name) {
  const guest::GuestText text(name);
  const guest::GuestBlock slot(4u);
  if (!text || !slot) {
    return 0u;
  }
  WR32(slot.address(), 0u);
  call.virtual_call(manager(call), kLoadModel,
                    {slot.address(), text.address(), kLoadKind});
  const std::uint32_t entity = RD32(slot.address());
  if (entity != 0u) {
    call.virtual_call(entity, kEntityStart, {0u});
  }
  return entity;
}

void release(guest::GuestCall &call, std::uint32_t entity) {
  const guest::GuestBlock slot(4u);
  if (entity == 0u || !slot) {
    return;
  }
  WR32(slot.address(), entity);
  call.cdecl_call(exe_linked(kRelease), {slot.address()});
}

void attach(guest::GuestCall &call, std::uint32_t entity) {
  call.virtual_call(manager(call), kAttach, {entity, 0u});
}

void detach(guest::GuestCall &call, std::uint32_t entity) {
  call.virtual_call(manager(call), kDetach, {entity, 0u});
}

void place(guest::GuestCall &call, Vectors &vectors, std::uint32_t entity,
           presentation::ScenePoint at, float depth, float sx, float sz,
           float turn) {
  if (entity == 0u) {
    return;
  }
  const std::uint32_t node = call.virtual_call(entity, kEntityTransform);
  call.virtual_call(node, kPlaceScaled,
                    {vectors.put(0, at.x, depth, at.z),
                     vectors.put(1, 0.0F, turn, 0.0F),
                     x2::guest::float_bits(sx), x2::guest::float_bits(1.0F),
                     x2::guest::float_bits(sz)});
}

void stretch(guest::GuestCall &call, Vectors &vectors, std::uint32_t entity,
             const input::SceneBox &art, const input::SceneBox &box,
             float depth) {
  if (!(box.width() > 0.0F) || !(art.width() > 0.0F) ||
      !(art.height() > 0.0F)) {
    place(call, vectors, entity, {kHidden, kHidden}, depth, 1.0F, 1.0F);
    return;
  }
  const float sx = box.width() / art.width();
  const float sz = box.height() / art.height();
  place(call, vectors, entity,
        {box.left - art.left * sx, box.top - art.top * sz}, depth, sx, sz);
}

input::SceneBox bounds(guest::GuestCall &call, Vectors &vectors,
                       std::uint32_t entity) {
  if (entity == 0u) {
    return {};
  }
  place(call, vectors, entity, {0.0F, 0.0F}, 0.0F, 1.0F, 1.0F);
  const std::uint32_t min = vectors.put(2, 0.0F, 0.0F, 0.0F);
  const std::uint32_t max = vectors.put(3, 0.0F, 0.0F, 0.0F);
  call.virtual_call(entity, kEntityBounds, {min, max});
  return {static_cast<float>(RDF32(min)), static_cast<float>(RDF32(min + 8u)),
          static_cast<float>(RDF32(max)), static_cast<float>(RDF32(max + 8u))};
}

std::uint32_t animation(guest::GuestCall &call, std::uint32_t entity) {
  if (entity == 0u) {
    return 0u;
  }
  const std::uint32_t holder = call.virtual_call(entity, kEntityAnimation);
  return holder == 0u ? 0u : call.virtual_call(holder, kAnimation);
}

void show_phase(guest::GuestCall &call, std::uint32_t entity, float phase) {
  const std::uint32_t anim = animation(call, entity);
  if (anim != 0u) {
    call.virtual_call(anim, kAnimationPhase, {x2::guest::float_bits(phase)});
  }
}

std::uint32_t style_color(guest::GuestCall &call, Vectors &vectors,
                          std::uint32_t style, bool selected, bool enabled,
                          std::uint32_t item, std::uint32_t custom) {
  const std::uint32_t out = vectors.put(3, 0.0F, 0.0F, 0.0F);
  call.thiscall(exe_linked(kStyleColor), style,
                {x2::guest::float_bits(selected ? 1.0F : 0.0F), out,
                 enabled ? 1u : 0u, item, custom});
  return call.cdecl_call(exe_linked(kPackColor), {out});
}

} // namespace x2::native::menu_scene
