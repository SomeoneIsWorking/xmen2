/*
 * vec3_override.cpp -- XMen2.exe's float vec3 leaves as native overrides over
 * vec3_math.cpp, each also completed in place as a JIT leaf.
 *
 * On a host without an x87 every one of these bodies runs each FLD/FMUL/FSTP
 * through the emulated register stack (issue #141).
 *
 * THE CONTRACT is the floats the body stores, ESP, the x87 TOP, tags, control
 * and status words, ST(0) for the dot product, and the EAX/ECX/EDX it leaves.
 * The bodies re-read their operands between stores, so an output that
 * overlaps an input sees its own earlier stores: the native code runs on a
 * snapshot of the touched bytes in the body's own load/store order and writes
 * the snapshot back, which is the same thing for any overlap.
 *
 * Each answers where x87_guest_order_applies() with the body's deepest push
 * (x87_exact.h). `math.vec3=0` turns them off; `math.vec3_verify` re-runs the
 * guest body after every native answer and aborts on any difference, and
 * needs an exact host.
 */
#include "vec3_math.hpp"
#include "x87_exact.h"

#include "guest_body.h"
#include "guest_memory.h"
#include "override_leaf.h"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <lucent/cvar_c.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace x2::native {
namespace {

constexpr const char *kModule = "XMen2.exe";
constexpr uint32_t kAffine = 0x005fcf70u;
constexpr uint32_t kAdd = 0x005fe5e0u;
constexpr uint32_t kSub = 0x005fe610u;
constexpr uint32_t kScale = 0x005fe640u;
constexpr uint32_t kDot = 0x005fe670u;
constexpr uint32_t kCross = 0x005fe690u;
constexpr uint32_t kVec3Bytes = 12u;
constexpr uint32_t kAffineBytes = 48u;

/* -1 until first use, then the cvars' answers. */
int s_enabled = -1;
int s_verify = -1;
uint64_t s_verified = 0;

bool enabled() {
  if (__builtin_expect(s_enabled < 0, 0)) {
    s_enabled = lucent_cvar_flag("math.vec3", 1) ? 1 : 0;
    s_verify = x87_verify_requested("math.vec3_verify");
  }
  return s_enabled != 0;
}

/* The guest bytes a body touches, as at most three disjoint ranges copied
   into one buffer. */
class OperandSnapshot {
public:
  /* False when a range wraps the address space. */
  bool add(uint32_t address, uint32_t bytes) {
    if (address + bytes < address) {
      return false;
    }
    Region range{address, address + bytes, 0};
    for (unsigned i = 0; i < count_;) {
      const Region &r = regions_[i];
      if (range.end < r.begin || r.end < range.begin) {
        i++;
        continue;
      }
      range.begin = std::min(range.begin, r.begin);
      range.end = std::max(range.end, r.end);
      regions_[i] = regions_[--count_];
    }
    regions_[count_++] = range;
    return true;
  }

  void load() {
    uint32_t offset = 0;
    for (unsigned i = 0; i < count_; i++) {
      regions_[i].offset = offset;
      offset += regions_[i].end - regions_[i].begin;
      guest_memory_read(regions_[i].begin, bytes_ + regions_[i].offset,
                        regions_[i].end - regions_[i].begin);
    }
  }

  float read(uint32_t address) const {
    float value;
    read_block(address, &value, sizeof value);
    return value;
  }

  void read_block(uint32_t address, void *out, uint32_t bytes) const {
    std::memcpy(out, at(address, bytes), bytes);
  }

  void write(uint32_t address, float value) {
    std::memcpy(const_cast<unsigned char *>(at(address, sizeof value)), &value,
                sizeof value);
  }

  /* Whether guest memory now holds exactly these bytes. */
  bool matches_guest() const {
    unsigned char guest[kCapacity];
    for (unsigned i = 0; i < count_; i++) {
      const uint32_t size = regions_[i].end - regions_[i].begin;
      guest_memory_read(regions_[i].begin, guest, size);
      if (std::memcmp(guest, bytes_ + regions_[i].offset, size) != 0) {
        return false;
      }
    }
    return true;
  }

  void commit() const {
    for (unsigned i = 0; i < count_; i++) {
      guest_memory_write(regions_[i].begin, bytes_ + regions_[i].offset,
                         regions_[i].end - regions_[i].begin);
    }
  }

private:
  /* Three operands of at most kAffineBytes + 2 * kVec3Bytes in all. */
  static constexpr uint32_t kCapacity = kAffineBytes + 2u * kVec3Bytes;
  struct Region {
    uint32_t begin;
    uint32_t end;
    uint32_t offset;
  };

  const unsigned char *at(uint32_t address, uint32_t bytes) const {
    for (unsigned i = 0; i < count_; i++) {
      if (address >= regions_[i].begin && address + bytes <= regions_[i].end) {
        return bytes_ + regions_[i].offset + (address - regions_[i].begin);
      }
    }
    std::abort(); /* every access lies inside a range add() was given */
  }

  Region regions_[3];
  unsigned count_ = 0;
  unsigned char bytes_[kCapacity];
};

/* The answer a body would leave: its registers, ESP and, for the dot
   product, the value it pushes. */
struct Answer {
  uint32_t eax;
  uint32_t ecx;
  uint32_t edx;
  uint32_t esp;
  bool pushes;
  x87_real st0;
};

void verify_or_abort(const CPU *C, uint32_t ep, const OperandSnapshot &native,
                     const Answer &answer) {
  CPU guest = *C;
  x86_guest_body(&guest, kModule, ep);
  CPU expect = *C;
  if (answer.pushes) {
    x87_push(&expect, answer.st0);
  }
  const bool same =
      guest.reg[kX86pEsp] == answer.esp && guest.reg[kX86pEax] == answer.eax &&
      guest.reg[kX86pEcx] == answer.ecx && guest.reg[kX86pEdx] == answer.edx &&
      guest.x87.top == expect.x87.top &&
      std::memcmp(guest.x87.tag, expect.x87.tag, sizeof guest.x87.tag) == 0 &&
      guest.x87.control == expect.x87.control &&
      guest.x87.status == expect.x87.status &&
      (!answer.pushes ||
       x87_require_st0(&guest, "vec3 verify") == answer.st0) &&
      native.matches_guest();
  if (!same) {
    x2_log_error("math.vec3_verify: the native %s!0x%08x disagrees with the "
                 "guest body: eax %08x/%08x, "
                 "ecx %08x/%08x, edx %08x/%08x, esp %08x/%08x, memory %s. "
                 "vec3_math.cpp is wrong; not "
                 "continuing.\n",
                 kModule, ep, answer.eax, guest.reg[kX86pEax], answer.ecx,
                 guest.reg[kX86pEcx], answer.edx, guest.reg[kX86pEdx],
                 answer.esp, guest.reg[kX86pEsp],
                 native.matches_guest() ? "same" : "differs");
    std::abort();
  }
  s_verified++;
  if ((s_verified & (s_verified - 1u)) == 0u) {
    x2_log_info("math.vec3_verify: %llu native answers match the guest body\n",
                (unsigned long long)s_verified);
  }
}

/* Store the answer, or check it against the guest body first. */
void finish(CPU *C, uint32_t ep, const OperandSnapshot &memory,
            const Answer &answer) {
  if (s_verify) {
    verify_or_abort(C, ep, memory, answer);
  }
  memory.commit();
  C->reg[kX86pEax] = answer.eax;
  C->reg[kX86pEcx] = answer.ecx;
  C->reg[kX86pEdx] = answer.edx;
  C->reg[kX86pEsp] = answer.esp;
  if (answer.pushes) {
    x87_push(C, answer.st0);
  }
}

/* cdecl affine(v, out, m): the three rows in order, each from the operands
   as the previous stores left them. Leaves EAX = m, ECX = v, EDX = out. */
int affine_native(CPU *C) {
  const uint32_t esp = C->reg[kX86pEsp];
  const uint32_t v = RD32(esp + 4u);
  const uint32_t out = RD32(esp + 8u);
  const uint32_t m = RD32(esp + 12u);
  OperandSnapshot memory;
  if (!enabled() || !x87_guest_order_applies(&C->x87, 2u) ||
      !memory.add(v, kVec3Bytes) || !memory.add(out, kVec3Bytes) ||
      !memory.add(m, kAffineBytes)) {
    return 0;
  }
  memory.load();
  for (unsigned row = 0; row < 3u; row++) {
    float vv[3];
    float mm[12];
    memory.read_block(v, vv, sizeof vv);
    memory.read_block(m, mm, sizeof mm);
    memory.write(out + 4u * row, vec3_affine_row(vv, mm, row));
  }
  finish(C, kAffine, memory, Answer{m, v, out, esp + 4u, false, 0});
  return 1;
}

/* thiscall this op= v (or s): element by element, ret 4. Leaves EAX = this
   and ECX = v, or ECX = this for the scale, whose operand is a float. */
template <float (*Op)(float, float), bool kScalar>
int elementwise_native(CPU *C, uint32_t ep) {
  const uint32_t esp = C->reg[kX86pEsp];
  const uint32_t self = C->reg[kX86pEcx];
  const uint32_t arg = RD32(esp + 4u);
  OperandSnapshot memory;
  if (!enabled() || !x87_guest_order_applies(&C->x87, 1u) ||
      !memory.add(self, kVec3Bytes) ||
      (!kScalar && !memory.add(arg, kVec3Bytes))) {
    return 0;
  }
  memory.load();
  float scalar;
  std::memcpy(&scalar, &arg, sizeof scalar);
  for (unsigned i = 0; i < 3u; i++) {
    const float operand = kScalar ? scalar : memory.read(arg + 4u * i);
    memory.write(self + 4u * i, Op(memory.read(self + 4u * i), operand));
  }
  finish(
      C, ep, memory,
      Answer{self, kScalar ? self : arg, C->reg[kX86pEdx], esp + 8u, false, 0});
  return 1;
}

int add_native(CPU *C) { return elementwise_native<vec3_add, false>(C, kAdd); }
int sub_native(CPU *C) { return elementwise_native<vec3_sub, false>(C, kSub); }
int scale_native(CPU *C) {
  return elementwise_native<vec3_scale, true>(C, kScale);
}

/* thiscall this . v into ST(0), ret 4. Leaves EAX = v. */
int dot_native(CPU *C) {
  const uint32_t esp = C->reg[kX86pEsp];
  const uint32_t self = C->reg[kX86pEcx];
  const uint32_t v = RD32(esp + 4u);
  OperandSnapshot memory;
  if (!enabled() || !x87_guest_order_applies(&C->x87, 2u) ||
      !memory.add(self, kVec3Bytes) || !memory.add(v, kVec3Bytes)) {
    return 0;
  }
  memory.load();
  float t[3];
  float vv[3];
  memory.read_block(self, t, sizeof t);
  memory.read_block(v, vv, sizeof vv);
  finish(C, kDot, memory,
         Answer{v, self, C->reg[kX86pEdx], esp + 8u, true, vec3_dot(t, vv)});
  return 1;
}

/* thiscall this = this x v, ret 4: `this` is loaded into registers first,
   v is read beside each store. Leaves EAX = v. */
int cross_native(CPU *C) {
  const uint32_t esp = C->reg[kX86pEsp];
  const uint32_t self = C->reg[kX86pEcx];
  const uint32_t v = RD32(esp + 4u);
  OperandSnapshot memory;
  if (!enabled() || !x87_guest_order_applies(&C->x87, 5u) ||
      !memory.add(self, kVec3Bytes) || !memory.add(v, kVec3Bytes)) {
    return 0;
  }
  memory.load();
  float t[3];
  memory.read_block(self, t, sizeof t);
  memory.write(self, vec3_cross_term(t[1], memory.read(v + 8u), t[2],
                                     memory.read(v + 4u)));
  memory.write(self + 4u, vec3_cross_term(t[2], memory.read(v), t[0],
                                          memory.read(v + 8u)));
  memory.write(self + 8u, vec3_cross_term(t[0], memory.read(v + 4u), t[1],
                                          memory.read(v)));
  finish(C, kCross, memory,
         Answer{v, self, C->reg[kX86pEdx], esp + 8u, false, 0});
  return 1;
}

/* An override runs its native half and the guest body where that declines;
   the leaf is the native half alone, declined while verify runs the body. */
#define X2_VEC3_OVERRIDE(name, ep)                                             \
  void override_##name(CPU *C) {                                               \
    if (!name##_native(C)) {                                                   \
      x86_guest_body(C, kModule, ep);                                          \
    }                                                                          \
  }                                                                            \
  int leaf_##name(CPU *C) { return enabled() && !s_verify && name##_native(C); }

X2_VEC3_OVERRIDE(affine, kAffine)
X2_VEC3_OVERRIDE(add, kAdd)
X2_VEC3_OVERRIDE(sub, kSub)
X2_VEC3_OVERRIDE(scale, kScale)
X2_VEC3_OVERRIDE(dot, kDot)
X2_VEC3_OVERRIDE(cross, kCross)

__attribute__((constructor)) void vec3_register_overrides() {
  const struct {
    uint32_t ep;
    void (*override_fn)(CPU *);
    x86_override_leaf_fn leaf;
  } table[] = {{kAffine, override_affine, leaf_affine},
               {kAdd, override_add, leaf_add},
               {kSub, override_sub, leaf_sub},
               {kScale, override_scale, leaf_scale},
               {kDot, override_dot, leaf_dot},
               {kCross, override_cross, leaf_cross}};
  for (const auto &entry : table) {
    x86_register_override(kModule, entry.ep, entry.override_fn);
    x86_register_override_leaf(kModule, entry.ep, entry.leaf);
  }
}

} // namespace
} // namespace x2::native
