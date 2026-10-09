/*
 * Known-answer tests for the guest printf format walker (src/native/crt.cpp).
 *
 * The walker exists because a va_list cannot be handed to libc from here: on
 * x86-32 cdecl the argument list IS a region of guest stack, so each directive
 * has to be pulled out by hand. That makes it a small parser, and a small
 * parser that is only ever exercised by the game is one whose bugs show up as
 * a wrong filename or a wrong key three subsystems away.
 *
 * A plain static array stands in for the guest stack. The test publishes the
 * 4 GB host window containing its static data as the guest-memory base, so the
 * same 32-bit-address translation used by the ARM64 runtime is exercised.
 */
#include "crt_format.h"
#include "guest_memory.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The runtime globals the header pulls in. */
uint32_t g_imgbase = 0x10000000U;
uint32_t g_image_lo, g_image_hi;
uint32_t g_fsbase, g_gsbase;

/* crt.cpp also owns the guest stdio table. These parser tests never open a
   file, so a reached file hook is a test-scope violation and must fail rather
   than quietly selecting a host path. */
const char *k32_open_path(const char *guest, int for_write) {
  (void)guest;
  (void)for_write;
  fprintf(stderr, "test_vformat: unexpected file-open path lookup\n");
  abort();
}

int k32_open_replaced(const char *guest, int for_write) {
  (void)guest;
  (void)for_write;
  fprintf(stderr, "test_vformat: unexpected replacement lookup\n");
  abort();
}

void k32_open_note(const char *guest, int ok, int replaced, const char *host) {
  (void)guest;
  (void)ok;
  (void)replaced;
  (void)host;
  fprintf(stderr, "test_vformat: unexpected file-open note\n");
  abort();
}

static int fails, checks;

static void eq(const char *what, const char *got, const char *want) {
  checks++;
  if (strcmp(got, want) != 0) {
    fails++;
    printf("  FAIL %-40s got \"%s\" want \"%s\"\n", what, got, want);
  }
}

/* A stand-in guest argument list: 4-byte slots, doubles taking two. */
static uint32_t slots[32];
static int nslots;
static void push_d(uint32_t v) { slots[nslots++] = v; }
static void push_f(double d) {
  union {
    double d;
    uint32_t u[2];
  } u;
  u.d = d;
  slots[nslots++] = u.u[0];
  slots[nslots++] = u.u[1];
}
static uint32_t gp(const void *pointer) {
  if (((uintptr_t)pointer & ~(uintptr_t)UINT32_MAX) != g_guest_memory_base) {
    fprintf(stderr, "test_vformat: static test data crossed its 4 GB "
                    "guest window\n");
    abort();
  }
  return guest_memory_address(pointer);
}
static uint32_t va(void) { return gp(slots); }

int main(void) {
  char out[256];

  g_guest_memory_base = (uintptr_t)slots & ~(uintptr_t)UINT32_MAX;

  nslots = 0;
  push_d(42);
  x2::native::guest_vformat(out, sizeof out, "n=%d", va());
  eq("plain %d", out, "n=42");

  nslots = 0;
  push_d(0xDEADBEEF);
  x2::native::guest_vformat(out, sizeof out, "%08X", va());
  eq("width+zero pad %08X", out, "DEADBEEF");

  nslots = 0;
  push_d(gp("hello"));
  x2::native::guest_vformat(out, sizeof out, "[%s]", va());
  eq("%s from a guest pointer", out, "[hello]");

  nslots = 0;
  push_d(0);
  x2::native::guest_vformat(out, sizeof out, "[%s]", va());
  eq("%s with a NULL pointer", out, "[(null)]");

  nslots = 0;
  push_f(1.5);
  x2::native::guest_vformat(out, sizeof out, "%.2f", va());
  eq("%f consumes EIGHT bytes", out, "1.50");

  /* The ordering case: a double in the middle must not desynchronise the
     arguments after it. This is the failure a 4-byte-per-arg walker gives. */
  nslots = 0;
  push_d(1);
  push_f(2.5);
  push_d(3);
  x2::native::guest_vformat(out, sizeof out, "%d %.1f %d", va());
  eq("int, double, int stay in step", out, "1 2.5 3");

  nslots = 0;
  push_d('x');
  x2::native::guest_vformat(out, sizeof out, "<%c>", va());
  eq("%c", out, "<x>");

  nslots = 0;
  push_d(7);
  x2::native::guest_vformat(out, sizeof out, "100%% of %d", va());
  eq("%% literal", out, "100% of 7");

  nslots = 0;
  push_d(5);
  push_d(42);
  x2::native::guest_vformat(out, sizeof out, "%*d", va());
  eq("star width consumes an argument", out, "   42");

  nslots = 0;
  {
    union {
      uint64_t u;
      uint32_t p[2];
    } q;
    q.u = 0x1122334455667788ULL;
    push_d(q.p[0]);
    push_d(q.p[1]);
  }
  x2::native::guest_vformat(out, sizeof out, "%I64x", va());
  eq("MSVC I64 spelling", out, "1122334455667788");

  /* Truncation: writes at most cap, always NUL-terminates, and reports the
     length it WOULD have needed. */
  nslots = 0;
  push_d(gp("abcdefghij"));
  {
    char small[5];
    int n = x2::native::guest_vformat(small, sizeof small, "%s", va());
    checks++;
    if (n != 10 || strcmp(small, "abcd") != 0) {
      fails++;
      printf("  FAIL truncation: n=%d buf=\"%s\" want n=10 buf=\"abcd\"\n", n,
             small);
    }
  }

  /* ---- the scanf walker ------------------------------------------- */
  /*
   * Same shape: a stand-in guest argument list of POINTERS, fed exactly what
   * a real call would give. The count matters as much as the values -- a
   * caller reads it to decide which of its locals were written, so a walker
   * that returns 3 having filled 2 corrupts by omission.
   */
  {
    uint32_t a1, a2;
    int r;
    static uint32_t v1, v2;
    nslots = 0;
    push_d(gp(&v1));
    push_d(gp(&v2));
    v1 = v2 = 0;
    r = x2::native::guest_vsscanf("12 34", "%d %d", va());
    checks++;
    if (r != 2 || v1 != 12 || v2 != 34) {
      fails++;
      printf("  FAIL scanf two ints: r=%d v1=%u v2=%u\n", r, v1, v2);
    }
    (void)a1;
    (void)a2;
  }
  {
    static char sbuf[32];
    static uint32_t num;
    int r;
    nslots = 0;
    push_d(gp(sbuf));
    push_d(gp(&num));
    sbuf[0] = 0;
    num = 0;
    r = x2::native::guest_vsscanf("name=alpha 7", "name=%s %d", va());
    checks++;
    if (r != 2 || strcmp(sbuf, "alpha") != 0 || num != 7) {
      fails++;
      printf("  FAIL scanf literal+%%s+%%d: r=%d s=\"%s\" n=%u\n", r, sbuf,
             num);
    }
  }
  { /* a literal that does not match must stop, and report what it DID fill */
    static uint32_t got;
    int r;
    nslots = 0;
    push_d(gp(&got));
    got = 0;
    r = x2::native::guest_vsscanf("x=5", "y=%d", va());
    checks++;
    if (r != 0 || got != 0) {
      fails++;
      printf("  FAIL scanf mismatched literal: r=%d got=%u\n", r, got);
    }
  }
  { /* suppression consumes input WITHOUT consuming an argument -- getting
       that wrong shifts every later store by one slot */
    static uint32_t second;
    int r;
    nslots = 0;
    push_d(gp(&second));
    second = 0;
    r = x2::native::guest_vsscanf("11 22", "%*d %d", va());
    checks++;
    if (r != 1 || second != 22) {
      fails++;
      printf("  FAIL scanf %%*d suppression: r=%d second=%u\n", r, second);
    }
  }
  { /* %hd must store TWO bytes, not four */
    static uint32_t box;
    int r;
    nslots = 0;
    push_d(gp(&box));
    box = 0xAAAAAAAAu;
    r = x2::native::guest_vsscanf("258", "%hd", va());
    checks++;
    if (r != 1 || (box & 0xFFFFu) != 258 || (box >> 16) != 0xAAAAu) {
      fails++;
      printf("  FAIL scanf %%hd width: r=%d box=0x%08x\n", r, box);
    }
  }
  { /* %f stores a float; %lf a double */
    static float f;
    static double dd;
    int r;
    nslots = 0;
    push_d(gp(&f));
    f = 0;
    r = x2::native::guest_vsscanf("2.5", "%f", va());
    checks++;
    if (r != 1 || f != 2.5f) {
      fails++;
      printf("  FAIL scanf %%f: r=%d f=%f\n", r, f);
    }
    nslots = 0;
    push_d(gp(&dd));
    dd = 0;
    r = x2::native::guest_vsscanf("2.5", "%lf", va());
    checks++;
    if (r != 1 || dd != 2.5) {
      fails++;
      printf("  FAIL scanf %%lf: r=%d d=%f\n", r, dd);
    }
  }

  { /* a scanset, and the negated form the engine actually uses */
    static char sc[64];
    int r;
    nslots = 0;
    push_d(gp(sc));
    sc[0] = 0;
    r = x2::native::guest_vsscanf("hello world", "%[^ ]", va());
    checks++;
    if (r != 1 || strcmp(sc, "hello") != 0) {
      fails++;
      printf("  FAIL scanf %%[^ ]: r=%d s=\"%s\"\n", r, sc);
    }
    nslots = 0;
    push_d(gp(sc));
    sc[0] = 0;
    r = x2::native::guest_vsscanf("abc123", "%[a-c]", va());
    checks++;
    if (r != 1 || strcmp(sc, "abc") != 0) {
      fails++;
      printf("  FAIL scanf %%[a-c]: r=%d s=\"%s\"\n", r, sc);
    }
  }

  { /* the real thing: the engine's identifier scanset, spelled out in
       full at 67 characters. A spec buffer too small to hold it reports
       "unterminated", which reads as a bad format in the game. */
    static char id[128];
    int r;
    nslots = 0;
    push_d(gp(id));
    id[0] = 0;
    r = x2::native::guest_vsscanf(
        " [ some_Name9./ ]",
        " [ "
        "%[_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./"
        "\\-] ]",
        va());
    checks++;
    if (r != 1 || strcmp(id, "some_Name9./") != 0) {
      fails++;
      printf("  FAIL scanf long scanset: r=%d s=\"%s\"\n", r, id);
    }
  }

  /* XMen2.exe's own format: MSVCR71 classifies '[' as CH_OTHER, which ends
     the directive and prints the character, so "%[" is a literal '['. */
  nslots = 0;
  push_d(gp("a"));
  push_d(gp("b"));
  push_d(gp("c"));
  x2::native::guest_vformat(out, sizeof out, "%s%[%s]%s", va());
  eq("%[ prints a literal [", out, "a[b]c");

  nslots = 0;
  push_d(7);
  x2::native::guest_vformat(out, sizeof out, "%-5!%d", va());
  eq("CH_OTHER drops pending flags/width", out, "!7");

  printf("vformat: %d of %d check(s) FAILED\n", fails, checks);
  if (!fails)
    printf("Established: printf %%d/%%x/%%s/%%c/%%f/%%%%, star width, MSVC's "
           "I64, a NULL %%s, MSVCR71 printing a CH_OTHER character such as %%[ "
           "literally, truncation, and that a double consumes two "
           "argument slots; scanf ints/string/float, a literal mismatch "
           "stopping with the right count, %%*d consuming input but not an "
           "argument, and %%hd storing two bytes rather than four.\n");
  return fails != 0;
}

/*
 * Stubs for the parts of the runtime crt.cpp references but this test never
 * reaches. They ABORT rather than returning a value: if the walker ever
 * depends on one of them, the test must fail loudly rather than pass on a
 * fabricated answer.
 */
#include <stdlib.h>
struct X86pCpu;
void x87_fault(const char *what) {
  fprintf(stderr, "test_vformat: x87_fault(%s) reached\n", what);
  abort();
}
void x86_guest_call(struct X86pCpu *C, uint32_t t) {
  (void)C;
  (void)t;
  fprintf(stderr, "test_vformat: x86_guest_call reached\n");
  abort();
}
void x86_guest_addr_of(uint32_t addr, const char **mod, uint32_t *guest) {
  (void)addr;
  (void)mod;
  (void)guest;
  fprintf(stderr, "test_vformat: x86_guest_addr_of reached\n");
  abort();
}
const char *win_path(const char *in) {
  (void)in;
  fprintf(stderr, "test_vformat: win_path reached\n");
  abort();
}
const char *x86_native_name_at(uint32_t a) {
  (void)a;
  fprintf(stderr, "test_vformat: x86_native_name_at reached\n");
  abort();
}
void x86_diag_dump(void) {
  fprintf(stderr, "test_vformat: x86_diag_dump reached\n");
  abort();
}
/* Guest threads arrived after this test did (issue #43), and crt.cpp's
   _beginthreadex/_endthreadex now call into src/native/threads.cpp. Aborting
   stubs rather than linking that file: this test is about the format walker,
   and a thread here would mean the walker took a path it has no business on. */
uint32_t guest_thread_create_ex(uint32_t start, uint32_t arg, uint32_t stack,
                                int suspended, uint32_t *tid) {
  (void)start;
  (void)arg;
  (void)stack;
  (void)suspended;
  (void)tid;
  fprintf(stderr, "test_vformat: guest_thread_create_ex reached\n");
  abort();
}
void guest_thread_exit(uint32_t code) {
  (void)code;
  fprintf(stderr, "test_vformat: guest_thread_exit reached\n");
  abort();
}
