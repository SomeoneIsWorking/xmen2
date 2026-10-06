#include "../native/x2_log.h"
/* See d3d8_com.h. */
#include "d3d8_com.h"

#include "guest_heap.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the interface tables --------------------------------------------- */

typedef struct {
  const char *name;
  signed char args; /* excluding this */
} MethodAbi;

typedef struct {
  D3D8IfaceId id;
  const char *name;
  int slot;
  unsigned long hits; /* only counted while unimplemented */
} MethodRec;

typedef struct {
  const char *name;
  const MethodAbi *abi;
  int nmethods;
  D3D8MethodFn *impl; /* nmethods entries, NULL where unimplemented */
  MethodRec *recs;    /* nmethods entries, one per slot */
  uint32_t vtable;    /* guest address, 0 until built */
} Iface;

/* Each interface's table, expanded from d3d8_abi.h. The slot number in the
   macro is checked against the array index at build time (see check_order),
   so a dropped or reordered line cannot shift the rest of the interface. */
#define D3D8_ABI_ENTRY(slot, name, args) {#name, args},

#define D3D8_DEFINE_ABI(iface)                                                 \
  static const MethodAbi ABI_##iface[] = {D3D8_IFACE_##iface(D3D8_ABI_ENTRY)};
D3D8_INTERFACES(D3D8_DEFINE_ABI)
#undef D3D8_DEFINE_ABI

/* The declared slot numbers, kept so they can be checked rather than trusted.
 */
#define D3D8_SLOT_ENTRY(slot, name, args) slot,
#define D3D8_DEFINE_SLOTS(iface)                                               \
  static const int SLOTS_##iface[] = {D3D8_IFACE_##iface(D3D8_SLOT_ENTRY)};
D3D8_INTERFACES(D3D8_DEFINE_SLOTS)
#undef D3D8_DEFINE_SLOTS

#define D3D8_IFACE_ROW(iface)                                                  \
  {#iface, ABI_##iface, (int)(sizeof ABI_##iface / sizeof ABI_##iface[0]),     \
   NULL,   NULL,        0},

static Iface g_iface[] = {D3D8_INTERFACES(D3D8_IFACE_ROW)};

static const int *const g_slots[] = {
#define D3D8_SLOTS_ROW(iface) SLOTS_##iface,
    D3D8_INTERFACES(D3D8_SLOTS_ROW)
#undef D3D8_SLOTS_ROW
};

static Iface *iface_of(D3D8IfaceId id) {
  if (id < 0 || id >= D3D8_IF_COUNT) {
    x2_log_error("d3d8: interface id %d is out of range\n", (int)id);
    abort();
  }
  return &g_iface[id];
}

const char *d3d8_iface_name(D3D8IfaceId id) { return iface_of(id)->name; }
int d3d8_iface_method_count(D3D8IfaceId id) { return iface_of(id)->nmethods; }

/*
 * Every entry must sit at the index it names.
 *
 * The X-macro writes the slot out explicitly precisely so this check exists.
 * A line dropped from the middle of IDirect3DDevice8 would otherwise move
 * forty methods up by one and the engine would call Clear expecting
 * SetTransform -- a failure that surfaces as garbage on screen, or as a stack
 * shift, and never mentions this file.
 */
static void check_order(void) {
  int i, k, bad = 0;
  for (i = 0; i < D3D8_IF_COUNT; i++)
    for (k = 0; k < g_iface[i].nmethods; k++)
      if (g_slots[i][k] != k) {
        x2_log_error("d3d8: %s entry %d declares slot %d\n", g_iface[i].name, k,
                     g_slots[i][k]);
        bad++;
      }
  if (bad) {
    x2_log_error("d3d8: %d ABI table entries are out of order. "
                 "Nothing may be dispatched through these vtables.\n",
                 bad);
    abort();
  }
}

/* ---- objects ----------------------------------------------------------- */

/*
 * What the guest sees. Twelve bytes, of which only the first word is defined
 * by anything outside this file: COM objects are opaque to their holders, and
 * the engine only ever dereferences [obj] to reach the vtable.
 *
 * The magic exists so that a pointer arriving as a this-pointer can be
 * REJECTED rather than misread. Before it, a guest pointer that was not ours
 * would have been treated as an object and the index word read from whatever
 * was there.
 */
#define D3D8_OBJ_MAGIC 0x38443344u /* "D3D8" little-endian */
#define D3D8_OBJ_SIZE 12u

struct D3D8Object {
  uint32_t guest;
  D3D8IfaceId iface;
  void *ctx;
  long refs;
  void (*destroy)(D3D8Object *);
  int live;
  D3D8Object *owner; /* see d3d8_object_set_owner */
};

static D3D8Object **g_objs;
static int g_nobjs, g_objcap;

D3D8Object *d3d8_object_new(D3D8IfaceId id, void *ctx) {
  Iface *f = iface_of(id);
  D3D8Object *o = (D3D8Object *)calloc(1, sizeof *o);
  uint32_t g = guest_malloc(D3D8_OBJ_SIZE);

  if (!o || !g) {
    x2_log_error("d3d8: out of memory creating a %s\n", f->name);
    abort();
  }
  if (g_nobjs == g_objcap) {
    int cap = g_objcap ? g_objcap * 2 : 32;
    D3D8Object **p = (D3D8Object **)realloc(g_objs, (size_t)cap * sizeof *p);
    if (!p) {
      x2_log_error("d3d8: out of memory\n");
      abort();
    }
    g_objs = p;
    g_objcap = cap;
  }
  o->guest = g;
  o->iface = id;
  o->ctx = ctx;
  o->refs = 1;
  o->live = 1;
  WR32(g + 0u, d3d8_iface_vtable(id));
  WR32(g + 4u, D3D8_OBJ_MAGIC);
  WR32(g + 8u, (uint32_t)g_nobjs);
  g_objs[g_nobjs++] = o;
  return o;
}

uint32_t d3d8_object_guest(const D3D8Object *o) { return o->guest; }
void *d3d8_object_ctx(const D3D8Object *o) { return o->ctx; }
D3D8IfaceId d3d8_object_iface(const D3D8Object *o) { return o->iface; }

void d3d8_object_set_destructor(D3D8Object *o, void (*fn)(D3D8Object *)) {
  o->destroy = fn;
}

void d3d8_object_set_owner(D3D8Object *o, D3D8Object *owner) {
  if (owner == o) {
    x2_log_error("d3d8: a %s was made its own container; refusing, or "
                 "AddRef would not terminate.\n",
                 g_iface[o->iface].name);
    return;
  }
  o->owner = owner;
}

D3D8Object *d3d8_object_owner(const D3D8Object *o) { return o->owner; }

long d3d8_object_addref(D3D8Object *o) {
  if (o->owner)
    return d3d8_object_addref(o->owner);
  if (!o->live)
    x2_log_error("d3d8: AddRef on a %s that was already released to "
                 "zero.\n",
                 g_iface[o->iface].name);
  return ++o->refs;
}

long d3d8_object_release(D3D8Object *o) {
  if (o->owner)
    return d3d8_object_release(o->owner);
  if (o->refs <= 0) {
    x2_log_error("d3d8: Release on a %s whose count is already 0. "
                 "Returning 0 rather than going negative -- but the "
                 "guest has released it more times than it took it.\n",
                 g_iface[o->iface].name);
    return 0;
  }
  if (--o->refs == 0) {
    if (o->destroy)
      o->destroy(o);
    /*
     * The object is RETIRED, not freed. Freeing it would return its guest
     * block to the arena, and a stale guest pointer would then land in
     * whatever was allocated next -- which reads as memory corruption
     * rather than as a use-after-release. Keeping it costs twelve bytes
     * and makes the mistake say its own name.
     */
    o->live = 0;
  }
  return o->refs;
}

long d3d8_object_refs(const D3D8Object *o) {
  if (!o)
    return 0;
  return o->owner ? d3d8_object_refs(o->owner) : o->refs;
}

void d3d8_object_report(void) {
  int i, live = 0, retired = 0, owned = 0;
  if (!g_nobjs) {
    x2_log_info("  d3d8: no COM object was ever created.\n");
    return;
  }
  for (i = 0; i < g_nobjs; i++) {
    if (g_objs[i]->owner)
      owned++;
    else if (g_objs[i]->live)
      live++;
    else
      retired++;
  }
  x2_log_info("  d3d8: %d COM object(s) -- %d still referenced, %d released to "
              "zero\n",
              g_nobjs - owned, live, retired);
  if (owned)
    x2_log_info("        and %d container-owned subresource(s), whose "
                "references are counted on their container\n",
                owned);
  for (i = 0; i < g_nobjs; i++)
    if (!g_objs[i]->owner && g_objs[i]->live && g_objs[i]->refs > 0)
      x2_log_info("        %-26s 0x%08x  %ld reference(s)\n",
                  g_iface[g_objs[i]->iface].name, g_objs[i]->guest,
                  g_objs[i]->refs);
}

D3D8Object *d3d8_object_from_guest(uint32_t g) {
  uint32_t idx;
  if (!g || RD32(g + 4u) != D3D8_OBJ_MAGIC)
    return NULL;
  idx = RD32(g + 8u);
  if (idx >= (uint32_t)g_nobjs)
    return NULL;
  return g_objs[idx]->guest == g ? g_objs[idx] : NULL;
}

/* ---- dispatch ---------------------------------------------------------- */

#define A(i) RD32(C->reg[kX86pEsp] + 4u + (uint32_t)(i) * 4u)

/* The method currently executing, so d3d8_ret knows what to pop and so a
   diagnostic anywhere below can name where it is. */
static const MethodRec *g_cur;
static const MethodAbi *g_cur_abi;
static int g_returned;
static int g_permissive;

const char *d3d8_current_method(void) {
  static char buf[128];
  if (!g_cur)
    return "(no D3D8 method is executing)";
  snprintf(buf, sizeof buf, "%s::%s (slot %d)", g_iface[g_cur->id].name,
           g_cur->name, g_cur->slot);
  return buf;
}

uint32_t d3d8_arg(CPU *C, int i) {
  if (!g_cur_abi) {
    x2_log_error("d3d8: d3d8_arg outside a dispatched method\n");
    abort();
  }
  if (i < 0 || i >= g_cur_abi->args) {
    x2_log_error("d3d8: %s takes %d argument(s); asked for #%d.\n"
                 "  Either the ABI table is wrong or this "
                 "implementation is reading past its arguments.\n",
                 d3d8_current_method(), g_cur_abi->args, i);
    abort();
  }
  return A(i + 1); /* +1: argument 0 is `this` */
}

float d3d8_argf(CPU *C, int i) {
  union {
    uint32_t u;
    float f;
  } v;
  v.u = d3d8_arg(C, i);
  return v.f;
}

void d3d8_ret(CPU *C, uint32_t hr) {
  if (!g_cur_abi) {
    x2_log_error("d3d8: d3d8_ret outside a dispatched method\n");
    abort();
  }
  /* __stdcall: the callee pops the return address, `this`, and every
     declared argument. The count comes from the ABI table rather than from
     the implementation, so an implementation cannot get it wrong. */
  C->reg[kX86pEax] = hr;
  C->reg[kX86pEsp] += 4u + 4u + (uint32_t)g_cur_abi->args * 4u;
  g_returned++;
}

/* The arguments the call was made with, as the guest pushed them.
 *
 * The name of an unimplemented method is the work item, but the ARGUMENTS are
 * what says how much work it is: `SetPixelShader(0)` is the engine turning the
 * fixed-function pipeline back on, while `SetPixelShader(<handle>)` means a
 * whole shader translator. Without these the only way to tell the two apart is
 * to implement something, rebuild and run again -- which is how this project
 * keeps spending forty minutes to learn one dword. */
static void print_args(const MethodAbi *abi, CPU *C, const char *lead) {
  int i;
  if (!abi->args) {
    x2_log_error("%sit takes no arguments.\n", lead);
    return;
  }
  x2_log_error("%sarguments as pushed:", lead);
  for (i = 0; i < abi->args; i++)
    x2_log_error(" [%d]=0x%08x", i, d3d8_arg(C, i));
  x2_log_error("\n");
}

static void report_unimplemented(MethodRec *r, const MethodAbi *abi, CPU *C) {
  if (g_permissive) {
    if (!r->hits++) {
      x2_log_error("d3d8: PERMISSIVE -- %s::%s (slot %d) ignored, "
                   "returning 0.\n",
                   g_iface[r->id].name, r->name, r->slot);
      print_args(abi, C, "  ");
    }
    d3d8_ret(C, 0);
    return;
  }
  x2_log_error(
      "\n*** the engine called %s::%s (slot %d, offset 0x%x), which this "
      "host D3D8 does not implement.\n",
      g_iface[r->id].name, r->name, r->slot, r->slot * 4);
  print_args(abi, C, "    ");
  x2_log_error(
      "    That name IS the work item. Implement it in src/d3d8/, or run "
      "with --d3d8-permissive to\n"
      "    walk past it and see what the engine asks for next -- knowing "
      "that whatever is drawn is missing it.\n");
  x86_diag_dump();
  abort();
}

static void dispatch(CPU *C) {
  MethodRec *r = (MethodRec *)x86_callback_ctx();
  Iface *f;
  const MethodAbi *abi;
  const MethodRec *prev_rec = g_cur;
  const MethodAbi *prev_abi = g_cur_abi;
  int prev_returned = g_returned;
  D3D8Object *self;
  uint32_t this_ptr;

  if (!r) {
    x2_log_error("d3d8: a vtable slot dispatched with no method "
                 "context; the callback was registered wrong.\n");
    abort();
  }
  f = &g_iface[r->id];
  abi = &f->abi[r->slot];

  g_cur = r;
  g_cur_abi = abi;
  g_returned = 0;

  this_ptr = A(0);
  self = d3d8_object_from_guest(this_ptr);
  if (!self) {
    x2_log_error(
        "\n*** %s::%s was called with this = 0x%08x, which is not an "
        "object this host created.\n"
        "    A COM pointer the guest holds came from somewhere else -- "
        "it is not safe to guess what it is.\n",
        f->name, r->name, this_ptr);
    x86_diag_dump();
    abort();
  }
  if (self->iface != r->id) {
    /* Possible and legitimate for the IUnknown prefix, but nothing else:
       a texture called through a device vtable is a bug in whoever handed
       the pointer over. */
    x2_log_error("\n*** %s::%s was called on a %s. The vtable and the object "
                 "disagree about what this is.\n",
                 f->name, r->name, g_iface[self->iface].name);
    abort();
  }

  if (f->impl && f->impl[r->slot])
    f->impl[r->slot](self, C);
  else
    report_unimplemented(r, abi, C);

  if (g_returned != 1) {
    x2_log_error("\n*** %s::%s called d3d8_ret %d times. Exactly one is "
                 "required: the guest stack is now %s, and the fault that "
                 "follows will name some unrelated function.\n",
                 f->name, r->name, g_returned,
                 g_returned ? "over-popped" : "short by this method's frame");
    abort();
  }
  g_cur = prev_rec;
  g_cur_abi = prev_abi;
  g_returned = prev_returned;
}

uint32_t d3d8_iface_vtable(D3D8IfaceId id) {
  Iface *f = iface_of(id);
  int k;

  if (f->vtable)
    return f->vtable;

  check_order();
  f->vtable = guest_malloc((uint32_t)f->nmethods * 4u);
  f->recs = (MethodRec *)calloc((size_t)f->nmethods, sizeof *f->recs);
  if (!f->vtable || !f->recs) {
    x2_log_error("d3d8: out of memory building %s's vtable\n", f->name);
    abort();
  }
  for (k = 0; k < f->nmethods; k++) {
    f->recs[k].id = id;
    f->recs[k].name = f->abi[k].name;
    f->recs[k].slot = k;
    WR32(f->vtable + (uint32_t)k * 4u,
         x86_native_callback(dispatch, f->name, f->abi[k].name, &f->recs[k]));
  }
  return f->vtable;
}

void d3d8_iface_implement(D3D8IfaceId id, const D3D8MethodFn *impl, int n) {
  Iface *f = iface_of(id);
  int k, have = 0;

  if (n != f->nmethods) {
    x2_log_error("d3d8: %s has %d methods but %d implementations were offered. "
                 "A short table would leave the tail reporting without saying "
                 "so.\n",
                 f->name, f->nmethods, n);
    abort();
  }
  d3d8_iface_vtable(id); /* records exist before we index */
  f->impl = (D3D8MethodFn *)calloc((size_t)n, sizeof *f->impl);
  if (!f->impl) {
    x2_log_error("d3d8: out of memory\n");
    abort();
  }
  for (k = 0; k < n; k++)
    if ((f->impl[k] = impl[k]) != NULL)
      have++;
  x2_log_info("  d3d8: %-26s %3d of %3d methods implemented\n", f->name, have,
              n);
}

/* ---- staging ----------------------------------------------------------- */

void d3d8_permissive(int on) { g_permissive = on; }

void d3d8_permissive_report(void) {
  int i, k, distinct = 0;
  unsigned long total = 0;

  if (!g_permissive)
    return;
  x2_log_info("\nd3d8: PERMISSIVE MODE ignored these methods -- anything drawn "
              "was produced WITHOUT them:\n");
  for (i = 0; i < D3D8_IF_COUNT; i++) {
    if (!g_iface[i].recs)
      continue;
    for (k = 0; k < g_iface[i].nmethods; k++)
      if (g_iface[i].recs[k].hits) {
        x2_log_info("        %-26s %-28s x%lu\n", g_iface[i].name,
                    g_iface[i].recs[k].name, g_iface[i].recs[k].hits);
        distinct++;
        total += g_iface[i].recs[k].hits;
      }
  }
  if (!distinct)
    x2_log_info("        (none -- every method the engine called was "
                "implemented)\n");
  else
    x2_log_info("      %d distinct method(s), %lu call(s).\n", distinct, total);
}

/* ---- self-test --------------------------------------------------------- */

/*
 * Proves the three things this file could be silently wrong about: that the
 * tables are in order, that a vtable slot reaches the method that slot names,
 * and that an unimplemented method REPORTS rather than doing nothing. The
 * third is the one worth testing -- the reporter is the diagnostic everything
 * downstream depends on, and a diagnostic that never fires is the failure this
 * project keeps finding.
 */
static int g_selftest_hits;
static uint32_t g_selftest_this;

static void selftest_method(D3D8Object *self, CPU *C) {
  g_selftest_hits++;
  g_selftest_this = d3d8_object_guest(self);
  d3d8_ret(C, 0x5A5A0000u | (uint32_t)d3d8_arg(C, 0));
}

int d3d8_com_selftest(void) {
  D3D8MethodFn impl[16];
  D3D8Object *o;
  CPU C;
  uint32_t sp, vt, slot_addr;
  int fails = 0, i;

  check_order();
  x2_log_info("d3d8: self-test -- %d interfaces\n", (int)D3D8_IF_COUNT);
  for (i = 0; i < D3D8_IF_COUNT; i++)
    x2_log_info("        %-28s %3d methods, vtable 0x%08x\n", g_iface[i].name,
                g_iface[i].nmethods, d3d8_iface_vtable((D3D8IfaceId)i));

  /* Dispatch through IDirect3D8 slot 6, GetAdapterModeCount(1 argument). */
  memset(impl, 0, sizeof impl);
  impl[6] = selftest_method;
  d3d8_iface_implement(D3D8_IF_IDirect3D8, impl,
                       d3d8_iface_method_count(D3D8_IF_IDirect3D8));

  o = d3d8_object_new(D3D8_IF_IDirect3D8, NULL);
  vt = d3d8_iface_vtable(D3D8_IF_IDirect3D8);
  if (RD32(d3d8_object_guest(o)) != vt) {
    x2_log_error("  FAIL: the object's first word is not its vtable\n");
    fails++;
  }
  slot_addr = RD32(vt + 6u * 4u);

  sp = guest_malloc(256);
  cpu_reset(&C);
  C.reg[kX86pEsp] = sp + 128u;
  WR32(C.reg[kX86pEsp], 0xDEADBEEFu);               /* return address */
  WR32(C.reg[kX86pEsp] + 4u, d3d8_object_guest(o)); /* this */
  WR32(C.reg[kX86pEsp] + 8u, 7u);                   /* Adapter */
  /* Dispatched directly rather than through x86_guest_call, which pushes a
     return address of its own and then RESTORES esp -- which would hide the
     one thing worth checking here, that the method popped exactly its own
     frame. */
  x86_dispatch(&C, slot_addr);

  if (g_selftest_hits != 1) {
    x2_log_error("  FAIL: slot 6 did not reach its implementation "
                 "(%d hits)\n",
                 g_selftest_hits);
    fails++;
  }
  if (g_selftest_this != d3d8_object_guest(o)) {
    x2_log_error("  FAIL: the method saw this = 0x%08x, not 0x%08x\n",
                 g_selftest_this, d3d8_object_guest(o));
    fails++;
  }
  if (C.reg[kX86pEax] != 0x5A5A0007u) {
    x2_log_error("  FAIL: returned 0x%08x, not 0x5A5A0007 -- the "
                 "argument did not arrive\n",
                 C.reg[kX86pEax]);
    fails++;
  }
  if (C.reg[kX86pEsp] != sp + 128u + 4u + 4u + 4u) {
    x2_log_error("  FAIL: esp is 0x%08x after a 1-argument method; "
                 "expected +12\n",
                 C.reg[kX86pEsp]);
    fails++;
  }

  /* The reporter must FIRE. Permissive mode is how it can be observed
     without aborting the process, and the count it keeps is the proof. */
  d3d8_permissive(1);
  cpu_reset(&C);
  C.reg[kX86pEsp] = sp + 128u;
  WR32(C.reg[kX86pEsp], 0xDEADBEEFu);
  WR32(C.reg[kX86pEsp] + 4u, d3d8_object_guest(o));
  WR32(C.reg[kX86pEsp] + 8u, 0u);
  x86_dispatch(&C, RD32(vt + 7u * 4u)); /* EnumAdapterModes */
  if (C.reg[kX86pEsp] != sp + 128u + 4u + 4u + 3u * 4u) {
    x2_log_error("  FAIL: the reporter popped the wrong amount for a "
                 "3-argument method (esp 0x%08x)\n",
                 C.reg[kX86pEsp]);
    fails++;
  }
  d3d8_permissive(0);

  x2_log_info("d3d8: self-test %s (%d failure%s)\n",
              fails ? "FAILED" : "passed", fails, fails == 1 ? "" : "s");
  return fails;
}
