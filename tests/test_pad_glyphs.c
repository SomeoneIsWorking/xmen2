#include "guest_memory.h"
#include "pad_glyph_codes.h"
#include "pad_glyphs.h"
#include "prompt_glyphs.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define SIZE 0x00700000u
#define BUFFER_RVA 0x0066aec8u
#define CONTROLLER0_RVA 0x00668f40u
#define ALT_SLOT 1u

static uint32_t mapped_base;
static X86Module module = {.name = "XMen2",
                           .base = &mapped_base,
                           .preferred = 0x00400000u,
                           .size = SIZE};
static int real_calls, reader_calls;
static uint32_t g_object;
static uint32_t g_heap_next;
static int active_pad = 0;
static int host_pad_for_slot[10];

uint32_t guest_malloc(uint32_t bytes) {
  uint32_t result;
  if (!g_heap_next)
    g_heap_next = mapped_base + 0x500000u;
  result = g_heap_next;
  g_heap_next += (bytes + 15u) & ~15u;
  return result;
}

X86Module *x86_modules(void) { return &module; }
int dinput8_controller_host_pad_for_slot(int controller_slot) {
  return controller_slot >= 0 && controller_slot < 10
             ? host_pad_for_slot[controller_slot]
             : -1;
}
int dinput_pad_uses_xbox_glyphs(int pad) { return pad == 1; }
int x2_player_input_pad_is_active_source(int pad) { return pad == active_pad; }
static void guest_body_006281f0(CPU *c) {
  real_calls++;
  c->eax = 0x12345678u;
  c->esp += 12u;
}
/* FUN_006294b0(row, slot, *kind, *code) -- the label's binding reader. The
   stub answers with a keyboard binding, so a super-call is visible as "the
   keyboard won" and the override's answer is visible as the pad. */
static void guest_body_006294b0(CPU *c) {
  uint32_t out_kind = RD32(c->esp + 0xcu), out_code = RD32(c->esp + 0x10u);
  reader_calls++;
  if (out_kind)
    WR32(out_kind, 1u); /* keyboard */
  if (out_code)
    WR32(out_code, 0x1cu); /* DIK Return */
  c->esp += 4u + 0x10u;
}
int x86_peek(uint32_t addr, void *out, size_t n) {
  if (addr < mapped_base || (uint64_t)addr + n > (uint64_t)mapped_base + SIZE)
    return 0;
  memcpy(out, guest_memory_const_pointer(addr), n);
  return 1;
}
int x86_peek32(uint32_t addr, uint32_t *out) {
  return x86_peek(addr, out, sizeof *out);
}
void x86_guest_call_args(CPU *c, uint32_t target, uint32_t pop) {
  (void)c;
  (void)target;
  (void)pop;
  abort(); /* the label path writes none */
}
/* FUN_00619e30 -- the label builder. The stub composes "[%s]" into the guest
   buffer exactly as the original's sprintf does, from a name the test picks. */
static uint32_t g_label_name;
static void guest_body_00619e30(CPU *c) {
  uint32_t out = mapped_base + BUFFER_RVA;
  uint32_t i = 0, ch;
  WR8(out, (uint8_t)'[');
  for (i = 0; (ch = RD8(g_label_name + i)) != 0; i++)
    WR8(out + 1u + i, (uint8_t)ch);
  WR8(out + 1u + i, (uint8_t)']');
  WR8(out + 2u + i, 0);
  c->eax = out;
  c->esp += 4u; /* cdecl: the caller cleans its arg */
}
/* FUN_004bd720 -- the token resolver prompt_labels.c probes for its consumer
   census. This test drives the label builder directly and never goes through
   the resolver, so reaching this body means the test exercised a path it does
   not model: say so rather than returning a plausible-looking pointer. */
static void guest_body_004bd720(CPU *c) {
  (void)c;
  fprintf(stderr, "test_pad_glyphs: the token resolver FUN_004bd720 was "
                  "entered, but this test does not model it.\n");
  abort();
}
void x86_seg_unset(const char *seg) {
  (void)seg;
  abort();
}
__thread uint32_t g_fsbase, g_gsbase;

void x2_override_006281f0(CPU *c);
void x2_override_006294b0(CPU *c);
void x2_override_00619e30(CPU *c);

/* Build a label from `name` and return what the override left in the buffer. */
static const char *label_after(const char *name) {
  CPU c = {0};
  uint32_t stack = mapped_base + 0x4000u, p = mapped_base + 0x4100u;
  uint32_t i;
  for (i = 0; name[i]; i++)
    WR8(p + i, (uint8_t)name[i]);
  WR8(p + i, 0);
  g_label_name = p;
  WR32(stack, 0xfeedfaceu);
  WR32(stack + 4u, 7u); /* the action id; the stub ignores it */
  c.esp = stack;
  x2_override_00619e30(&c);
  return guest_memory_const_pointer(c.eax);
}

/* Write a binding straight into the guest table, using the ABI
   input_bindings.c states: element = object + (row*4 + slot)*12, kind at +4,
   code at +8. */
static void put_binding(uint32_t row, uint32_t slot, uint32_t kind,
                        uint32_t code) {
  uint32_t at = g_object + (row * 4u + slot) * 12u;
  WR32(at + 4u, kind);
  WR32(at + 8u, code);
}

/* Ask the label reader for a row and report what it answered. */
static int reader_says(uint32_t row, uint32_t *kind, uint32_t *code,
                       int want_super) {
  CPU c = {0};
  uint32_t stack = mapped_base + 0x2000u;
  int before = reader_calls;
  WR32(stack, 0xfeedfaceu);
  WR32(stack + 4u, row);
  WR32(stack + 8u, 2u); /* slot 2, the label's first choice */
  WR32(stack + 0xcu, mapped_base + 0x3000u);
  WR32(stack + 0x10u, mapped_base + 0x3004u);
  c.esp = stack;
  c.ecx = g_object; /* __thiscall: the binding object */
  x2_override_006294b0(&c);
  if (c.esp != stack + 0x14u)
    return 0;
  if ((reader_calls - before != 0) != (want_super != 0))
    return 0;
  *kind = RD32(mapped_base + 0x3000u);
  *code = RD32(mapped_base + 0x3004u);
  return 1;
}

static int check_call(uint32_t kind, uint32_t code, uint32_t want,
                      int want_real) {
  CPU c = {0};
  uint32_t *stack = guest_memory_pointer(mapped_base + 0x1000u);
  int before = real_calls;
  stack[0] = 0xfeedfaceu;
  stack[1] = kind;
  stack[2] = code;
  c.esp = mapped_base + 0x1000u;
  x2_override_006281f0(&c);
  if (c.esp != mapped_base + 0x100cu || real_calls - before != want_real)
    return 0;
  if (want_real)
    return c.eax == 0x12345678u;
  return c.eax == mapped_base + BUFFER_RVA && RD8(c.eax) == want &&
         RD8(c.eax + 1u) == 0;
}

int main(int argc, char **argv) {
  int ok;
  (void)argc;
  mapped_base = 0x10000000u;
  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(mapped_base, SIZE, PROT_READ | PROT_WRITE) != 0) {
    perror("test_pad_glyphs guest map");
    return 1;
  }
  memset(host_pad_for_slot, 0xff, sizeof host_pad_for_slot);
  host_pad_for_slot[0] = 1; /* guest slot 0 is Xbox-family host pad 1 */
  host_pad_for_slot[1] = 0; /* guest slot 1 is generic host pad 0 */
  if (argc == 2 && strcmp(argv[1], "--disabled") == 0) {
    setenv("X2_PROMPT_GLYPHS", "0", 1);
    unsetenv("X2_PAD_GLYPHS");
    ok = check_call(3, 0x15, 0, 1);
    printf("pad glyph disabled gate: %s\n", ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
  }

  setenv("X2_PROMPT_GLYPHS", "1", 1);
  ok = check_call(3, 0x15, 0x80, 0) && /* A */
       check_call(3, 5, 0x86, 0) &&    /* Z+ = LT */
       check_call(3, 6, 0x87, 0) &&    /* Z- = RT */
       /* One glyph PER d-pad direction. Checking that each POV code
          returns SOME glyph is what the old single-icon mapping already
          did; what separates the two is that the four must DIFFER (#88). */
       check_call(3, 0x11, X2_PAD_GLYPH_DPAD_RIGHT, 0) &&
       check_call(3, 0x12, X2_PAD_GLYPH_DPAD_LEFT, 0) &&
       check_call(3, 0x13, X2_PAD_GLYPH_DPAD_DOWN, 0) &&
       check_call(3, 0x14, X2_PAD_GLYPH_DPAD_UP, 0) &&
       check_call(3, 0x1d, X2_PAD_GLYPH_LS, 0) &&
       check_call(3, 0x1e, X2_PAD_GLYPH_RS, 0) &&
       check_call(4, 0x15, 0, 1) && /* generic host pad */
       check_call(5, 0x15, 0, 1);   /* unresolved guest slot */
  if (!ok) {
    fprintf(stderr, "pad glyph shipping-wrapper checks FAILED\n");
    return 1;
  }
  /* And no two codes in the whole vocabulary share a glyph: a prompt that
     draws the same picture for two different bindings is the defect, and
     one pair of it is as wrong as four. */
  {
    uint8_t seen[256];
    unsigned code, distinct = 0, collisions = 0;
    memset(seen, 0, sizeof seen);
    for (code = 1u; code <= 0x1eu; code++) {
      uint8_t g = pad_glyph_code(code);
      if (!g)
        continue;
      if (seen[g]++) {
        fprintf(stderr, "pad glyph: code 0x%02x reuses glyph 0x%02x\n", code,
                g);
        collisions++;
      }
      distinct++;
    }
    printf("pad glyph vocabulary: %u code(s) map to a glyph, %u "
           "collision(s)\n",
           distinct, collisions);
    if (collisions)
      return 1;
  }
  /*
   * The label-selection half. Without it the naming override above can be
   * perfectly correct and never appear: FUN_00619e30 asks for slot 2 first,
   * and slot 2 of the set it reads is where the game puts its own menu keys
   * (row 4 slot 2 is Return), so the keyboard always wins with a pad in
   * hand. Both directions are checked -- a row WITH a pad binding must be
   * named by it whatever slot it sits in, and a row without one must
   * super-call and keep the game's own answer.
   */
  {
    uint32_t controller = mapped_base + 0x10000u;
    uint32_t kind = 0, code = 0;
    g_object = controller + 0x18u;
    WR32(mapped_base + CONTROLLER0_RVA, controller);
    memset(guest_memory_pointer(g_object), 0, 42u * 4u * 12u);

    put_binding(4u, ALT_SLOT, 3u, 0x15u); /* guest 0 -> host 1 */
    put_binding(4u, 2u, 1u, 0x1cu);       /* and the menu key beside it */
    active_pad = 1;
    if (!reader_says(4u, &kind, &code, 0) || kind != 3u || code != 0x15u) {
      fprintf(stderr,
              "pad glyph label: a row with a pad binding was not "
              "named by it (kind %u code 0x%02x)\n",
              kind, code);
      return 1;
    }
    active_pad = -1;
    if (!reader_says(4u, &kind, &code, 1) || kind != 1u || code != 0x1cu) {
      fprintf(stderr, "pad glyph label: keyboard-active hotswap did not "
                      "retain the keyboard prompt\n");
      return 1;
    }
    put_binding(6u, ALT_SLOT, 4u, 0x16u); /* guest 1 -> generic host 0 */
    active_pad = 0;
    if (!reader_says(6u, &kind, &code, 0) || kind != 4u || code != 0x16u) {
      fprintf(stderr, "pad glyph label: a non-Xbox active pad was not "
                      "selected for the retail naming path\n");
      return 1;
    }
    active_pad = 1;
    if (!reader_says(6u, &kind, &code, 1) || kind != 1u || code != 0x1cu) {
      fprintf(stderr, "pad glyph label: activity from a different host "
                      "pad selected the reordered guest slot\n");
      return 1;
    }
    put_binding(7u, 2u, 1u, 0x1cu); /* keyboard only */
    if (!reader_says(7u, &kind, &code, 1) || kind != 1u || code != 0x1cu) {
      fprintf(stderr,
              "pad glyph label: a row with no pad binding did not "
              "super-call (kind %u code 0x%02x)\n",
              kind, code);
      return 1;
    }

    /* Reordering is live: swapping the authoritative mapping changes
       which physical family and active source each guest kind names. */
    host_pad_for_slot[0] = 0;
    host_pad_for_slot[1] = 1;
    if (!check_call(3, 0x15, 0, 1) ||
        !check_call(4, 0x15, X2_PAD_GLYPH_FACE_A, 0)) {
      fprintf(stderr, "pad glyph label: live slot reorder did not "
                      "retarget glyph family\n");
      return 1;
    }
  }
  /* The label presentation. Pad pictures lose brackets. Keyboard bindings
     keep their live text but receive composable keycap pieces around it. */
  {
    char want[64];
    size_t at = 0, i;
    char glyph[2];
    glyph[0] = (char)X2_PAD_GLYPH_FACE_A;
    glyph[1] = '\0';
    if (strcmp(label_after(glyph), glyph) != 0) {
      fprintf(stderr,
              "pad glyph brackets: a glyph label kept its "
              "brackets (%s)\n",
              label_after(glyph));
      return 1;
    }
    want[at++] = (char)X2_KEYCAP_GLYPH_LEFT;
    for (i = 0; i < 5; i++)
      want[at++] = (char)X2_KEYCAP_GLYPH_MIDDLE;
    for (i = 0; i < 5; i++)
      want[at++] = (char)X2_KEYCAP_GLYPH_REWIND;
    memcpy(want + at, "ENTER", 5);
    at += 5;
    want[at++] = (char)X2_KEYCAP_GLYPH_RIGHT;
    want[at] = '\0';
    if (strcmp(label_after("ENTER"), want) != 0) {
      fprintf(stderr, "prompt keycap: ENTER was not composed from the "
                      "four scalable pieces\n");
      return 1;
    }
    at = 0;
    want[at++] = (char)X2_KEYCAP_GLYPH_LEFT;
    want[at++] = (char)X2_KEYCAP_GLYPH_MIDDLE;
    want[at++] = (char)X2_KEYCAP_GLYPH_REWIND;
    want[at++] = 'A';
    want[at++] = (char)X2_KEYCAP_GLYPH_RIGHT;
    want[at] = '\0';
    if (strcmp(label_after("A"), want) != 0) {
      fprintf(stderr, "prompt keycap: one-character A was not composed\n");
      return 1;
    }
    if (strcmp(label_after("???"), "[???]") != 0) {
      fprintf(stderr,
              "pad glyph brackets: the unmapped label changed "
              "(%s)\n",
              label_after("???"));
      return 1;
    }
  }

  {
    /* Font occupancy is process-global truth. A byte owned by any retail
       font must leave both production paths: pad naming super-calls the
       retail name, and label presentation keeps a foreign glyph bracketed
       instead of pairing its metrics with native art. */
    char foreign[2] = {(char)X2_PAD_GLYPH_FACE_A, 0};
    char bracketed[4] = {'[', (char)X2_PAD_GLYPH_FACE_A, ']', 0};
    x2_prompt_glyph_mark_unavailable(X2_PAD_GLYPH_FACE_A);
    if (!check_call(4, 0x15, 0, 1)) {
      fprintf(stderr, "pad glyph availability: an occupied codepoint "
                      "did not use the retail naming body\n");
      return 1;
    }
    if (strcmp(label_after(foreign), bracketed) != 0) {
      fprintf(stderr, "pad glyph availability: a foreign codepoint "
                      "was still presented as native art\n");
      return 1;
    }

    x2_prompt_glyph_mark_unavailable(X2_KEYCAP_GLYPH_MIDDLE);
    if (strcmp(label_after("ENTER"), "[ENTER]") != 0) {
      fprintf(stderr, "keycap availability: a keycap with an occupied "
                      "piece did not retain the retail label\n");
      return 1;
    }
  }
  printf("pad glyph wrapper: enabled, reordered, unresolved and disabled "
         "mappings plus pad/keycap/unmapped/occupied label cases passed\n");
  return 0;
}

/*
 * The retail bodies these tests super-call into. Production reaches them
 * through x86_guest_body, so the test models the same seam rather than a
 * symbol per function -- and an entry point this test does not model is a
 * FAILURE that names itself, never a silent return.
 */
void x86_guest_body(CPU *C, const char *module, uint32_t linked_ep) {
  if (linked_ep == 0x006281f0u && !strcmp(module, "XMen2.exe")) {
    guest_body_006281f0(C);
    return;
  }
  if (linked_ep == 0x006294b0u && !strcmp(module, "XMen2.exe")) {
    guest_body_006294b0(C);
    return;
  }
  if (linked_ep == 0x00619e30u && !strcmp(module, "XMen2.exe")) {
    guest_body_00619e30(C);
    return;
  }
  if (linked_ep == 0x004bd720u && !strcmp(module, "XMen2.exe")) {
    guest_body_004bd720(C);
    return;
  }
  fprintf(stderr,
          "%s: x86_guest_body(%s, 0x%08x) is not modelled by this test.\n",
          "test_pad_glyphs.c", module, linked_ep);
  abort();
}
