#include "outline_kerning.hpp"

#include <algorithm>
#include <bit>
#include <utility>

namespace x2::native {

namespace {

constexpr std::uint32_t kKernTag = 0x6b65726eu; /* 'kern' */
constexpr std::uint16_t kPairPos = 2u;
constexpr std::uint16_t kExtension = 9u;
constexpr std::uint16_t kXAdvance = 0x0004u;

/* Bytes in a ValueRecord of this format. */
std::size_t value_size(std::uint16_t format) {
  return 2u * (std::size_t)std::popcount((unsigned)(format & 0xffu));
}

/* The XAdvance field's offset inside a ValueRecord of this format. */
std::size_t x_advance_at(std::uint16_t format) {
  return 2u * (std::size_t)std::popcount((unsigned)(format & 0x3u));
}

} // namespace

GposKerning::GposKerning(std::vector<std::uint8_t> table)
    : table_(std::move(table)) {
  if (table_.size() < 10u || u16(0) != 1u) {
    return;
  }
  const std::size_t features = u16(6);
  const std::size_t lookups = u16(8);
  std::vector<std::uint16_t> wanted;
  const unsigned feature_count = u16(features);
  for (unsigned i = 0; i < feature_count; i++) {
    const std::size_t record = features + 2u + i * 6u;
    if (u32(record) != kKernTag) {
      continue;
    }
    const std::size_t feature = features + u16(record + 4u);
    const unsigned count = u16(feature + 2u);
    for (unsigned k = 0; k < count; k++) {
      wanted.push_back(u16(feature + 4u + k * 2u));
    }
  }
  std::sort(wanted.begin(), wanted.end());
  wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
  const unsigned lookup_count = u16(lookups);
  for (const std::uint16_t index : wanted) {
    if (index < lookup_count) {
      add_lookup(lookups + u16(lookups + 2u + index * 2u));
    }
  }
}

std::uint16_t GposKerning::u16(std::size_t at) const {
  if (at + 2u > table_.size()) {
    return 0;
  }
  return (std::uint16_t)(table_[at] << 8 | table_[at + 1u]);
}

std::uint32_t GposKerning::u32(std::size_t at) const {
  return (std::uint32_t)u16(at) << 16 | u16(at + 2u);
}

void GposKerning::add_lookup(std::size_t lookup) {
  const std::uint16_t type = u16(lookup);
  const unsigned count = u16(lookup + 4u);
  std::vector<std::size_t> pairs;
  for (unsigned i = 0; i < count; i++) {
    std::size_t subtable = lookup + u16(lookup + 6u + i * 2u);
    std::uint16_t kind = type;
    if (kind == kExtension) {
      kind = u16(subtable + 2u);
      subtable += u32(subtable + 4u);
    }
    if (kind == kPairPos) {
      pairs.push_back(subtable);
    }
  }
  if (!pairs.empty()) {
    subtables_.push_back(std::move(pairs));
  }
}

int GposKerning::coverage_index(std::size_t coverage,
                                std::uint16_t glyph) const {
  const std::uint16_t format = u16(coverage);
  const unsigned count = u16(coverage + 2u);
  for (unsigned i = 0; i < count; i++) {
    if (format == 1u) {
      if (u16(coverage + 4u + i * 2u) == glyph) {
        return (int)i;
      }
    } else if (format == 2u) {
      const std::size_t range = coverage + 4u + i * 6u;
      if (glyph >= u16(range) && glyph <= u16(range + 2u)) {
        return (int)(u16(range + 4u) + (glyph - u16(range)));
      }
    }
  }
  return -1;
}

unsigned GposKerning::glyph_class(std::size_t class_def,
                                  std::uint16_t glyph) const {
  const std::uint16_t format = u16(class_def);
  if (format == 1u) {
    const std::uint16_t start = u16(class_def + 2u);
    if (glyph >= start && glyph - start < u16(class_def + 4u)) {
      return u16(class_def + 6u + (glyph - start) * 2u);
    }
  } else if (format == 2u) {
    const unsigned count = u16(class_def + 2u);
    for (unsigned i = 0; i < count; i++) {
      const std::size_t range = class_def + 4u + i * 6u;
      if (glyph >= u16(range) && glyph <= u16(range + 2u)) {
        return u16(range + 4u);
      }
    }
  }
  return 0;
}

bool GposKerning::pair(std::size_t subtable, std::uint16_t first,
                       std::uint16_t second, int *out) const {
  const std::uint16_t format = u16(subtable);
  const int covered = coverage_index(subtable + u16(subtable + 2u), first);
  if (covered < 0) {
    return false;
  }
  const std::uint16_t format1 = u16(subtable + 4u);
  const std::uint16_t format2 = u16(subtable + 6u);
  const std::size_t record = 2u + value_size(format1) + value_size(format2);
  if (format == 1u) {
    if ((unsigned)covered >= u16(subtable + 8u)) {
      return false;
    }
    const std::size_t set = subtable + u16(subtable + 10u + covered * 2u);
    const unsigned count = u16(set);
    for (unsigned i = 0; i < count; i++) {
      const std::size_t value = set + 2u + i * record;
      if (u16(value) == second) {
        *out = (format1 & kXAdvance)
                   ? (std::int16_t)u16(value + 2u + x_advance_at(format1))
                   : 0;
        return true;
      }
    }
    return false;
  }
  if (format != 2u) {
    return false;
  }
  const unsigned class1 = glyph_class(subtable + u16(subtable + 8u), first);
  const unsigned class2 = glyph_class(subtable + u16(subtable + 10u), second);
  const unsigned class1_count = u16(subtable + 12u);
  const unsigned class2_count = u16(subtable + 14u);
  if (class1 >= class1_count || class2 >= class2_count) {
    return false;
  }
  const std::size_t pair_size = record - 2u;
  const std::size_t value =
      subtable + 16u + (class1 * class2_count + class2) * pair_size;
  *out = (format1 & kXAdvance)
             ? (std::int16_t)u16(value + x_advance_at(format1))
             : 0;
  return true;
}

int GposKerning::adjustment(std::uint16_t first, std::uint16_t second) const {
  int total = 0;
  for (const std::vector<std::size_t> &lookup : subtables_) {
    for (const std::size_t subtable : lookup) {
      int value = 0;
      if (pair(subtable, first, second, &value)) {
        total += value;
        break;
      }
    }
  }
  return total;
}

} // namespace x2::native
