#include "controller_hotplug.h"

#include <assert.h>
#include <stdio.h>

static int checks;
#define CHECK(c) do { assert(c); checks++; } while (0)

int main(void)
{
    X2ControllerHotplug state = {0};
    uint64_t generation = 0;
    int cycle;

    CHECK(x2_controller_hotplug_needs_admission(&state, generation));
    x2_controller_hotplug_enumerated(&state, generation, 0, 0);
    CHECK(!x2_controller_hotplug_needs_admission(&state, generation));

    /* More cycles than the simultaneous eight-pad inventory limit. Every
       attach and detach is admitted once, and an unchanged pump never storms. */
    for (cycle = 0; cycle < 12; cycle++) {
        generation++;
        CHECK(x2_controller_hotplug_needs_admission(&state, generation));
        x2_controller_hotplug_admitted(&state);
        x2_controller_hotplug_enumerated(&state, generation, 1, 1);
        CHECK(!x2_controller_hotplug_needs_admission(&state, generation));

        generation++;
        CHECK(x2_controller_hotplug_needs_admission(&state, generation));
        x2_controller_hotplug_admitted(&state);
        x2_controller_hotplug_enumerated(&state, generation, 0, 0);
        CHECK(!x2_controller_hotplug_needs_admission(&state, generation));
    }
    CHECK(state.admissions == 24);
    CHECK(state.connected == 0);
    CHECK(state.last_reported == 0);

    printf("controller_hotplug: %d checks passed\n", checks);
    return 0;
}
