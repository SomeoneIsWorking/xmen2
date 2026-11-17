#ifndef X2_CONVERSATION_PANEL_HPP
#define X2_CONVERSATION_PANEL_HPP

#include "x86rt.h"

#include <cstdint>

/* The conversation singleton's panel (external/xmen2-decomp/docs/notes/cutscene_player.md): its flags,
   the highlighted response, its two text boxes and its models. */
namespace x2::native::conversation {

inline constexpr std::uint32_t kSingleton = 0x00717aacu;

/* 0x2 shown (FUN_004556b0). 0x10 only says the panel is loaded for the map
   (FUN_00455af0 sets it, FUN_00455d90 clears it), conversation or not. */
inline constexpr std::uint32_t kFlags = 0x21b24u;
inline constexpr std::uint8_t kVisible = 0x02u;

/* Shown: the party takes no damage and level-ups wait. */
constexpr bool holds(std::uint8_t flags) { return (flags & kVisible) != 0u; }

/* The current line's id, and the highlighted response of those drawn
   (shorts). */
inline constexpr std::uint32_t kCurrentLine = 0x4bcu;
inline constexpr std::uint32_t kTagIndex = 0x21b26u;
inline constexpr std::uint32_t kTagCount = 0x21b28u;

/* Float: the game time before which accept does not advance the line. */
inline constexpr std::uint32_t kAcceptAfter = 0x21b5cu;
/* FUN_0046dce0, the game owner; its slot 0x160 is the game time. */
inline constexpr std::uint32_t kGameOwner = 0x0046dce0u;
inline constexpr std::uint32_t kGameTime = 0x160u;

/* The update's accept gate (0x0045d1a0): accept advances the line now. */
bool takes_accept(const CPU &cpu, std::uint32_t self);

/* Loaded at init (FUN_00455af0): ui/hud/convo_background, then the line's
   and the responses' text boxes, of the popup's text box kind
   (popup_dialog.hpp). */
inline constexpr std::uint32_t kBackground = 0x21b18u;
inline constexpr std::uint32_t kLineBox = 0x21b38u;
inline constexpr std::uint32_t kResponseBox = 0x21b3cu;

/* A record's id, its children's ids and how many
   (src/native/conversation.cpp). */
inline constexpr std::uint32_t kRecordId = 0x88u;
inline constexpr std::uint32_t kRecordChildren = 0x8cu;
inline constexpr std::uint32_t kRecordChildCount = 0xacu;
inline constexpr std::uint32_t kMaxChildren = 8u;

} // namespace x2::native::conversation

#endif
