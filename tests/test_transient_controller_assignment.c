#include "transient_controller_assignment.h"

#include "dinput_pad.h"
#include "settings.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char guid[DINPUT_PAD_MAX][16];
static int connected[DINPUT_PAD_MAX];
static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

int dinput_pad_instance_guid(int pad, unsigned char out[16]) {
  if (pad < 0 || pad >= DINPUT_PAD_MAX || !connected[pad])
    return 0;
  memcpy(out, guid[pad], 16);
  return 1;
}

int dinput_pad_for_guid(const unsigned char wanted[16]) {
  int pad;
  for (pad = 0; pad < DINPUT_PAD_MAX; pad++)
    if (connected[pad] && memcmp(guid[pad], wanted, 16) == 0)
      return pad;
  return -1;
}

const char *dinput_pad_persistent_id(int pad) {
  static const char *const ID[] = {"sdl-session-pad-a", "sdl-session-pad-b"};
  return pad >= 0 && pad < 2 ? ID[pad] : NULL;
}

int main(void) {
  const char *path = X2_TEST_TRANSIENT_SETTINGS_PATH;
  X2Settings settings, loaded;
  char why[128];

  memset(guid[0], 0x11, 16);
  memset(guid[1], 0x22, 16);
  connected[0] = connected[1] = 1;
  x2_transient_controller_reset();
  CHECK(x2_transient_controller_assign(0, 0));
  CHECK(x2_transient_controller_has_assignment(0));
  CHECK(x2_transient_controller_resolve(0) == 0);
  CHECK(x2_transient_controller_player_for_pad(0) == 0);
  CHECK(strcmp(x2_transient_controller_id(0), "sdl-session-pad-a") == 0);

  CHECK(x2_transient_controller_assign(0, 2));
  CHECK(!x2_transient_controller_has_assignment(0));
  CHECK(x2_transient_controller_player_for_pad(0) == 2);
  CHECK(x2_transient_controller_assign(1, 2));
  CHECK(x2_transient_controller_player_for_pad(0) == -1);
  CHECK(x2_transient_controller_resolve(2) == 1);

  connected[1] = 0;
  CHECK(x2_transient_controller_has_assignment(2));
  CHECK(x2_transient_controller_resolve(2) == -1);
  memset(guid[1], 0x33, 16); /* a different pad reuses slot one */
  connected[1] = 1;
  CHECK(x2_transient_controller_resolve(2) == -1);
  CHECK(x2_transient_controller_player_for_pad(1) == -1);

  remove(path);
  x2_settings_defaults(&settings);
  CHECK(x2_settings_save(&settings, path, why, sizeof why));
  x2_transient_controller_reset(); /* process restart */
  CHECK(x2_settings_load(&loaded, path, why, sizeof why));
  CHECK(!x2_transient_controller_has_assignment(2));
  CHECK(x2_settings_player_controller(&loaded, 2) == NULL);
  remove(path);

  printf("test_transient_controller_assignment: %d checks passed\n", checks);
  return 0;
}
