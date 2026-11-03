#ifndef X2_XMLB_TREE_HPP
#define X2_XMLB_TREE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/* Alchemy's binary XML (XMLB), read into a tree and written back laid out
   the way the shipped files are, so an unchanged tree round-trips byte for
   byte. The format is documented in vendor/shared/alchemy/tools/xmlb.py. */
namespace x2::native::xmlb {

struct Node {
  std::string name;
  std::vector<std::pair<std::string, std::string>> attrs;
  std::vector<Node> children;

  [[nodiscard]] std::optional<std::string_view> get(std::string_view key) const;
  void set(std::string_view key, std::string_view value);
};

/* The tree, or nothing when `data` is not complete XMLB. */
std::optional<Node> parse(const std::vector<std::uint8_t> &data);
std::vector<std::uint8_t> serialise(const Node &root);

} // namespace x2::native::xmlb

#endif
