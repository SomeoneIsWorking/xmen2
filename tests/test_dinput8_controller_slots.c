#include "dinput8_controller_slots.h"
#include "guest_memory.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* Large enough to cover the POLL side too: FUN_006285c0 reads its device
   array at manager+0xc and its per-frame polled mask at manager+0x129cc. */
#define MANAGER_BYTES 0x14000u
#define ATTACHED_OFFSET 0x4e4u
#define INSTANCE_OFFSET 0x27e8u
#define DEVICE_ARRAY_OFFSET 0x0cu
#define POLLED_MASK_OFFSET 0x129ccu
#define MANAGER_ADDRESS 0x70000000u

static unsigned char host_guid[2][16] = {{0xa1}, {0xb2}};
static int checks;

#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "test_dinput8_controller_slots:%d: %s failed\n",         \
              __LINE__, #c);                                                   \
      return 1;                                                                \
    }                                                                          \
    checks++;                                                                  \
  } while (0)

int dinput_pad_instance_guid(int host_pad, unsigned char guid[16]) {
  if (host_pad < 0 || host_pad >= 2)
    return 0;
  memcpy(guid, host_guid[host_pad], 16);
  return 1;
}

int dinput_pad_count(void) { return 2; }

int dinput_pad_for_guid(const unsigned char guid[16]) {
  int host_pad;
  for (host_pad = 0; host_pad < 2; host_pad++)
    if (memcmp(guid, host_guid[host_pad], 16) == 0)
      return host_pad;
  return -1;
}

static void set_poll(uint32_t manager, int controller_slot, uint32_t device,
                     int polled) {
  unsigned char *base = guest_memory_pointer(manager);
  uint32_t *devices = (uint32_t *)(base + DEVICE_ARRAY_OFFSET);
  uint32_t *mask = (uint32_t *)(base + POLLED_MASK_OFFSET);

  devices[controller_slot] = device;
  if (polled)
    *mask |= 1u << controller_slot;
  else
    *mask &= ~(1u << controller_slot);
}

static void set_slot(uint32_t manager, int controller_slot, int attached,
                     const unsigned char guid[16]) {
  unsigned char *base = guest_memory_pointer(manager);
  base[ATTACHED_OFFSET + (unsigned)controller_slot] = (unsigned char)attached;
  memcpy(base + INSTANCE_OFFSET + (unsigned)controller_slot * 16u, guid, 16);
}

int main(void) {
  unsigned char unknown[16] = {0xc3};
  uint32_t manager = MANAGER_ADDRESS;

  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(manager, MANAGER_BYTES, PROT_READ | PROT_WRITE) !=
          0) {
    perror("test_dinput8_controller_slots guest map");
    return 1;
  }
  dinput8_controller_slots_set_manager(manager);

  set_slot(manager, 0, 1, host_guid[1]);
  set_slot(manager, 2, 1, host_guid[0]);
  set_slot(manager, 4, 1, unknown);
  CHECK(dinput8_controller_slot_for_host_pad(0) == 2);
  CHECK(dinput8_controller_slot_for_host_pad(1) == 0);
  CHECK(dinput8_controller_host_pad_for_slot(0) == 1);
  CHECK(dinput8_controller_host_pad_for_slot(2) == 0);
  CHECK(dinput8_controller_host_pad_for_slot(4) == -1);
  CHECK(dinput8_controller_host_pad_for_slot(-1) == -1);
  CHECK(dinput8_controller_host_pad_for_slot(10) == -1);

  set_slot(manager, 0, 0, host_guid[1]);
  CHECK(dinput8_controller_slot_for_host_pad(1) == -1);
  CHECK(dinput8_controller_host_pad_for_slot(0) == -1);

  set_slot(manager, 0, 1, host_guid[0]);
  set_slot(manager, 2, 1, host_guid[1]);
  CHECK(dinput8_controller_slot_for_host_pad(0) == 0);
  CHECK(dinput8_controller_slot_for_host_pad(1) == 2);
  CHECK(dinput8_controller_host_pad_for_slot(0) == 0);
  CHECK(dinput8_controller_host_pad_for_slot(2) == 1);

  /* The poll-side probe. Issue #117's whole shape is a slot the game
     SKIPS, so the probe is only worth anything if it says so out loud: it
     must name the NULL slot, and it must not describe an unpublished
     manager the same way it describes ten empty slots. Both answers are
     demanded here rather than assumed. */
  {
    char report[4096];
    size_t len;

    set_poll(manager, 0, 0x0badf00du, 1);
    set_poll(manager, 2, 0u, 0);
    len = dinput8_controller_slots_probe(report, sizeof report);
    CHECK(len > 0 && len < sizeof report);
    CHECK(strstr(report, "slot 0") && strstr(report, "0x0badf00d"));
    CHECK(strstr(report, "last frame READ"));
    CHECK(strstr(report, "slot 2  device 0x00000000  NULL -- SKIPPED"));
    CHECK(strstr(report, "1 of 10 slot(s) hold a device interface"));

    /* A slot holding a device that the loop did NOT read last frame is a
       different failure from an empty slot, and must read differently. */
    set_poll(manager, 2, 0x0c0ffeeu, 0);
    len = dinput8_controller_slots_probe(report, sizeof report);
    CHECK(len > 0);
    CHECK(!strstr(report, "slot 2  device 0x00000000  NULL -- SKIPPED"));
    CHECK(strstr(report, "slot 2  device 0x00c0ffee"));
    CHECK(strstr(report, "2 of 10 slot(s) hold a device interface"));
    CHECK(strstr(report, "1 read last frame"));

    dinput8_controller_slots_set_manager(0);
    len = dinput8_controller_slots_probe(report, sizeof report);
    CHECK(len > 0);
    CHECK(strstr(report, "has not been published"));
    CHECK(!strstr(report, "slot 0"));
    dinput8_controller_slots_set_manager(manager);
  }

  dinput8_controller_slots_set_manager(0);
  CHECK(dinput8_controller_slot_for_host_pad(0) == -1);
  CHECK(dinput8_controller_host_pad_for_slot(0) == -1);

  printf("dinput8_controller_slots: %d checks passed\n", checks);
  return 0;
}
