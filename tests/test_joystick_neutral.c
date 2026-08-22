#include "joystick_neutral.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(c) do { assert(c); checks++; } while (0)

static uint32_t word(const unsigned char *state, unsigned offset)
{
    uint32_t value;
    memcpy(&value, state + offset, sizeof value);
    return value;
}

int main(void)
{
    unsigned char state[272];
    unsigned i;

    memset(state, 0x80, sizeof state);
    CHECK(x2_joystick_write_neutral(state, sizeof state, -1000, 2000));
    for (i = 0; i < 8u; i++) CHECK(word(state, i * 4u) == 500u);
    for (i = 0; i < 4u; i++) CHECK(word(state, 32u + i * 4u) == UINT32_MAX);
    for (i = 48u; i < sizeof state; i++) CHECK(state[i] == 0u);

    memset(state, 0x80, sizeof state);
    CHECK(!x2_joystick_write_neutral(state, 100u, -1000, 1000));
    for (i = 0; i < 100u; i++) CHECK(state[i] == 0u);

    printf("test_joystick_neutral: %d checks passed\n", checks);
    return 0;
}
