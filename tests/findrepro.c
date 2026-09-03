/* Focused reproducer for C022: recompiled igTObjectList<igController>::find
 * faults where the original does not.
 *
 * The fuzzer only says "it faults sometimes". This builds the object by hand so
 * the inputs are known, and prints them, so the failing shape is identifiable
 * rather than inferred.
 *
 * Layout used by find(), read off its disassembly:
 *   this+0x08 = element count (signed)
 *   this+0x10 = pointer to the element array
 * find(value, startIndex) scans [startIndex, count) for `value`.
 */
#include "x86rt.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

void fn_10008280(CPU *C); /* the recompiled find */

typedef int(__thiscall *find_t)(void *self, void *value, int start);

static uint8_t g_stack[0x8000];

static uint32_t call_recomp(void *self, uint32_t value, uint32_t start) {
  CPU C;
  uint32_t sp;
  memset(&C, 0, sizeof C);
  sp = (uint32_t)(uintptr_t)(g_stack + sizeof g_stack - 0x80);
  *(uint32_t *)(uintptr_t)(sp + 0) = 0xDEADBEEFU;
  *(uint32_t *)(uintptr_t)(sp + 4) = value;
  *(uint32_t *)(uintptr_t)(sp + 8) = start;
  C.esp = sp;
  C.ecx = (uint32_t)(uintptr_t)self;
  fn_10008280(&C);
  return C.eax;
}

int main(void) {
  HMODULE h = LoadLibraryA("libIGDisplay_orig.dll");
  find_t orig;
  uint8_t obj[0x40];
  void *elems[8];
  int i, bad = 0, ran = 0;
  static const int starts[] = {0, 1, 3, 4, 5, 7, -1, -2, 0x7FFFFFFF};
  static const int counts[] = {0, 1, 4, 8, -1, -8};

  if (!h) {
    printf("cannot load DLL -- tested NOTHING\n");
    return 2;
  }
  g_imgbase = (uint32_t)(uintptr_t)h;
  orig = (find_t)GetProcAddress(
      h, "?find@?$igTObjectList@VigController@Display@Gap@@@Core@Gap@@"
         "QBEHPBVigController@Display@3@H@Z");
  if (!orig) {
    printf("symbol not found -- tested NOTHING\n");
    return 2;
  }

  for (i = 0; i < 8; i++)
    elems[i] = (void *)(uintptr_t)(0x1000 + i);

  for (i = 0; i < (int)(sizeof counts / sizeof counts[0]); i++) {
    int ci;
    for (ci = 0; ci < (int)(sizeof starts / sizeof starts[0]); ci++) {
      int want, got, s = starts[ci], c = counts[i];
      void *needle = elems[3];
      memset(obj, 0, sizeof obj);
      *(int *)(obj + 0x08) = c;
      *(void **)(obj + 0x10) = elems;

      want = orig(obj, needle, s);
      got = (int)call_recomp(obj, (uint32_t)(uintptr_t)needle, (uint32_t)s);
      ran++;
      if (want != got) {
        printf("  count=%-11d start=%-11d orig=%-11d recomp=%d\n", c, s, want,
               got);
        bad++;
      }
    }
  }
  printf("-- %d combinations, %d mismatches\n", ran, bad);
  if (!ran) {
    printf("-- NOTHING RAN\n");
    return 2;
  }
  return bad ? 1 : 0;
}
