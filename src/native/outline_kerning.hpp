/* Pair kerning from an OpenType GPOS table: the x-advance adjustments of the
 * lookups the `kern` feature names (PairPos formats 1 and 2, directly or
 * through an extension). See external/xmen2-decomp/docs/notes/text.md. */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace x2::native {

class GposKerning {
public:
  /* The raw GPOS table; an empty or malformed table kerns nothing. */
  explicit GposKerning(std::vector<std::uint8_t> table);

  /* The first glyph's advance adjustment before the second, in font units. */
  int adjustment(std::uint16_t first, std::uint16_t second) const;

  bool empty() const { return subtables_.empty(); }

private:
  std::uint16_t u16(std::size_t at) const;
  std::uint32_t u32(std::size_t at) const;
  int coverage_index(std::size_t coverage, std::uint16_t glyph) const;
  unsigned glyph_class(std::size_t class_def, std::uint16_t glyph) const;
  /* The adjustment of one PairPos subtable; false when it does not apply. */
  bool pair(std::size_t subtable, std::uint16_t first, std::uint16_t second,
            int *out) const;
  void add_lookup(std::size_t lookup);

  std::vector<std::uint8_t> table_;
  /* PairPos subtables grouped by lookup: the first that applies wins. */
  std::vector<std::vector<std::size_t>> subtables_;
};

} // namespace x2::native
