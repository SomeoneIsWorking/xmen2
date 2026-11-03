#include "xmlb_tree.hpp"

#include <cstring>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace x2::native::xmlb {
namespace {

constexpr std::uint32_t kMagic = 0x000011b1u;
constexpr std::uint32_t kVersion = 1u;
constexpr std::uint32_t kNone = 0xffffffffu;
constexpr int kMaxDepth = 64;
constexpr std::uint32_t kMaxAttrs = 4096u;

class Reader {
public:
  explicit Reader(const std::vector<std::uint8_t> &data) : data_(data) {}

  bool u32(std::size_t at, std::uint32_t *out) const {
    if (at + 4u > data_.size()) {
      return false;
    }
    std::memcpy(out, data_.data() + at, 4u);
    return true;
  }

  bool text(std::uint32_t at, std::string *out) const {
    if (at >= data_.size()) {
      return false;
    }
    const auto *start = data_.data() + at;
    const void *end = std::memchr(start, 0, data_.size() - at);
    if (end == nullptr) {
      return false;
    }
    out->assign(reinterpret_cast<const char *>(start),
                static_cast<const std::uint8_t *>(end) - start);
    return true;
  }

  bool node(std::uint32_t at, int depth, Node *out) const {
    std::uint32_t name = 0;
    std::uint32_t child = 0;
    std::uint32_t count = 0;
    if (depth > kMaxDepth || !u32(at, &name) || !u32(at + 8u, &child) ||
        !u32(at + 12u, &count) || count > kMaxAttrs ||
        !text(name, &out->name)) {
      return false;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
      std::uint32_t key = 0;
      std::uint32_t value = 0;
      std::pair<std::string, std::string> attr;
      if (!u32(at + 16u + i * 8u, &key) || !u32(at + 20u + i * 8u, &value) ||
          !text(key, &attr.first) || !text(value, &attr.second)) {
        return false;
      }
      out->attrs.push_back(std::move(attr));
    }
    std::unordered_set<std::uint32_t> seen;
    for (std::uint32_t next = child; next != kNone;) {
      if (!seen.insert(next).second) {
        return false;
      }
      Node sub;
      if (!node(next, depth + 1, &sub) || !u32(next + 4u, &next)) {
        return false;
      }
      out->children.push_back(std::move(sub));
    }
    return true;
  }

private:
  const std::vector<std::uint8_t> &data_;
};

void put(std::vector<std::uint8_t> &out, std::uint32_t value) {
  const auto at = out.size();
  out.resize(at + 4u);
  std::memcpy(out.data() + at, &value, 4u);
}

} // namespace

std::optional<std::string_view> Node::get(std::string_view key) const {
  for (const auto &[k, v] : attrs) {
    if (k == key) {
      return v;
    }
  }
  return std::nullopt;
}

void Node::set(std::string_view key, std::string_view value) {
  for (auto &[k, v] : attrs) {
    if (k == key) {
      v = value;
      return;
    }
  }
  attrs.emplace_back(key, value);
}

std::optional<Node> parse(const std::vector<std::uint8_t> &data) {
  const Reader reader(data);
  std::uint32_t magic = 0;
  std::uint32_t version = 0;
  Node root;
  if (data.size() < 24u || !reader.u32(0, &magic) || magic != kMagic ||
      !reader.u32(4, &version) || version != kVersion ||
      !reader.node(8u, 0, &root)) {
    return std::nullopt;
  }
  return root;
}

std::vector<std::uint8_t> serialise(const Node &root) {
  /* A node, then its subtree, then its next sibling; the string pool after
     the last node, each string stored once at its first use. */
  std::vector<const Node *> order;
  const std::function<void(const Node &)> walk = [&](const Node &n) {
    order.push_back(&n);
    for (const Node &c : n.children) {
      walk(c);
    }
  };
  walk(root);
  std::unordered_map<const Node *, std::uint32_t> offset;
  std::uint32_t at = 8u;
  for (const Node *n : order) {
    offset[n] = at;
    at += 16u + 8u * static_cast<std::uint32_t>(n->attrs.size());
  }
  const std::uint32_t pool_base = at;
  std::vector<std::uint8_t> pool;
  std::unordered_map<std::string, std::uint32_t> where;
  const auto intern = [&](const std::string &s) {
    const auto found = where.find(s);
    if (found != where.end()) {
      return found->second;
    }
    const auto o = pool_base + static_cast<std::uint32_t>(pool.size());
    where.emplace(s, o);
    pool.insert(pool.end(), s.begin(), s.end());
    pool.push_back(0);
    return o;
  };
  intern(root.name);
  std::vector<std::uint8_t> out;
  put(out, kMagic);
  put(out, kVersion);
  const std::function<void(const Node &, std::uint32_t)> emit =
      [&](const Node &n, std::uint32_t next) {
        put(out, intern(n.name));
        put(out, next);
        put(out, n.children.empty() ? kNone : offset[&n.children.front()]);
        put(out, static_cast<std::uint32_t>(n.attrs.size()));
        for (const auto &[k, v] : n.attrs) {
          put(out, intern(k));
          put(out, intern(v));
        }
        for (std::size_t i = 0; i < n.children.size(); ++i) {
          emit(n.children[i], i + 1u < n.children.size()
                                  ? offset[&n.children[i + 1u]]
                                  : kNone);
        }
      };
  emit(root, kNone);
  out.insert(out.end(), pool.begin(), pool.end());
  return out;
}

} // namespace x2::native::xmlb
