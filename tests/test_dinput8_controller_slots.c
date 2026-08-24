#include "dinput8_controller_slots.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define MANAGER_BYTES          0x3000u
#define ATTACHED_OFFSET        0x4e4u
#define INSTANCE_OFFSET        0x27e8u

static unsigned char host_guid[2][16] = {{0xa1}, {0xb2}};
static int checks;

#define CHECK(c) do { \
    if (!(c)) { \
        fprintf(stderr, "test_dinput8_controller_slots:%d: %s failed\n", \
                __LINE__, #c); \
        return 1; \
    } \
    checks++; \
} while (0)

int dinput_pad_instance_guid(int host_pad, unsigned char guid[16])
{
    if (host_pad < 0 || host_pad >= 2) return 0;
    memcpy(guid, host_guid[host_pad], 16);
    return 1;
}

int dinput_pad_for_guid(const unsigned char guid[16])
{
    int host_pad;
    for (host_pad = 0; host_pad < 2; host_pad++)
        if (memcmp(guid, host_guid[host_pad], 16) == 0) return host_pad;
    return -1;
}

static void set_slot(uint32_t manager, int controller_slot, int attached,
                     const unsigned char guid[16])
{
    unsigned char *base = (unsigned char *)(uintptr_t)manager;
    base[ATTACHED_OFFSET + (unsigned)controller_slot] =
        (unsigned char)attached;
    memcpy(base + INSTANCE_OFFSET + (unsigned)controller_slot * 16u, guid, 16);
}

int main(void)
{
    unsigned char unknown[16] = {0xc3};
    void *region = mmap(NULL, MANAGER_BYTES, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    uint32_t manager;

    if (region == MAP_FAILED || (uintptr_t)region > UINT32_MAX) {
        perror("test_dinput8_controller_slots mmap");
        return 77;
    }
    manager = (uint32_t)(uintptr_t)region;
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

    dinput8_controller_slots_set_manager(0);
    CHECK(dinput8_controller_slot_for_host_pad(0) == -1);
    CHECK(dinput8_controller_host_pad_for_slot(0) == -1);

    printf("dinput8_controller_slots: %d checks passed\n", checks);
    return 0;
}
