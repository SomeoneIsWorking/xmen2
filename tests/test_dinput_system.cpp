#include "dinput_system.h"

#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

int main(void) {
  /* The shipping translation table, not a test copy. Pause row 17 defaults
     to DIK 0x01 in XMen2.exe; this proves SDL Escape reaches that byte. */
  CHECK(dinput_system_dik(SDL_SCANCODE_ESCAPE) == 0x01u);
  CHECK(dinput_system_dik(SDL_SCANCODE_RETURN) == 0x1cu);
  CHECK(dinput_system_dik(SDL_SCANCODE_UNKNOWN) == 0u);
  CHECK(strcmp(dinput_system_dik_name(0x01u), "Escape") == 0);
  CHECK(strcmp(dinput_system_dik_name(0x1cu), "Return") == 0);
  CHECK(dinput_system_dik_name(0u) == NULL);

  printf("test_dinput_system: %d scancode-to-DIK checks passed\n", checks);
  return 0;
}
