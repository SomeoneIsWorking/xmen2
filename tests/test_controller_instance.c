#include "controller_instance.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char live_guid[16];
static int live_slot = -1;
static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

int dinput_pad_for_guid(const unsigned char guid[16]) {
  return live_slot >= 0 && memcmp(guid, live_guid, sizeof live_guid) == 0
             ? live_slot
             : -1;
}

int main(void) {
  X2ControllerInstance old_instance;
  X2ControllerInstance new_instance;
  unsigned char a[16] = {0xA1};
  unsigned char b[16] = {0xB2};

  memcpy(live_guid, a, sizeof a);
  live_slot = 0;
  x2_controller_instance_bind(&old_instance, a);
  CHECK(x2_controller_instance_matches(&old_instance, a));
  CHECK(!x2_controller_instance_matches(&old_instance, b));
  CHECK(x2_controller_instance_resolve(&old_instance) == 0);

  live_slot = -1;
  CHECK(x2_controller_instance_resolve(&old_instance) == -1);

  /* A new connection reuses slot zero. The old object stays lost because
     its GUID did not change; only the newly bound object resolves to it. */
  memcpy(live_guid, b, sizeof b);
  live_slot = 0;
  x2_controller_instance_bind(&new_instance, b);
  CHECK(x2_controller_instance_resolve(&old_instance) == -1);
  CHECK(x2_controller_instance_resolve(&new_instance) == 0);

  printf("controller_instance: %d checks passed\n", checks);
  return 0;
}
