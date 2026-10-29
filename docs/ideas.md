# Ideas

Maybes, not yet asked for as work. A plan goes in `docs/plans/` once one is chosen.

## A scalable font for the game's text

The retail text engine draws glyphs from bitmap atlases loaded by `loadFonts`
(`FUN_005980f0`, `ui/fonts/fonts_pc.xmlb` / `fonts_HD.xmlb`; see
[Text](RE/text.md)). Touch layouts draw that text larger than retail did, and a
phone's screen is around 2728x1264. So the glyphs are magnified textures, soft at
the edges: the PDA's Back footer on the phone is an example.

A scalable font keeps the retail layout (the engine still measures and wraps)
and replaces only the glyph pixels. The port already does this for prompt art:
it publishes metrics into private codepoint records and draws its own atlas,
which this would generalise. The candidate sources:

- **An outline font that matches the game's typeface,** rendered as an SDF/MSDF
  atlas at the screen's resolution. Its license must allow shipping it.
- **A distance field derived at runtime from the player's own retail atlas.**
  This stays faithful to the game's glyphs, but it cannot add detail the bitmap
  never had.
