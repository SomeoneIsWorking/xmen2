#include "d3d8_light_selftest.h"

#include "d3d8_device.h"
#include "d3d8_types.h"
#include "guest_heap.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { CONTROL_LIGHT_INDEX = 51 };

_Static_assert(D3D8_MAX_LIGHTS > CONTROL_LIGHT_INDEX,
               "the D3D8 light table must hold the stock game's slot 51");

void d3d8_light_selftest_configure(D3D8State *state)
{
    float *light = state->light[CONTROL_LIGHT_INDEX];

    ((uint32_t *)light)[0] = 3u;                       /* directional */
    light[1] = light[2] = light[3] = light[4] = 1.0f;
    light[16] = 0.0f;
    light[17] = 0.0f;
    light[18] = 1.0f;
    light[19] = 1000.0f;
    state->light_set[CONTROL_LIGHT_INDEX] = 1;
    state->light_on[CONTROL_LIGHT_INDEX] = 1;
}

int d3d8_light_selftest(D3D8SelftestCall call)
{
    D3D8Object *device;
    uint32_t args[2], light_address;
    float *light, retained[3];
    uint32_t result;
    int fails = 0;

    printf("\n=== d3d8 light-slot selftest: stock index 51 through the "
           "device vtable ===\n");
    d3d8_device_install();
    device = d3d8_object_new(D3D8_IF_IDirect3DDevice8, NULL);
    if (!device) {
        printf("d3d8 light-slot selftest: FAILED -- no device object.\n");
        return 1;
    }

    /* D3DLIGHT8 is 26 dwords. The SetLight diagnostic also observes the
       engine record's adjacent original-position triplet at +0x68, so keep
       that tail addressable instead of manufacturing an unrelated warning. */
    light_address = guest_malloc((26u + 3u) * sizeof(uint32_t));
    if (!light_address) {
        printf("d3d8 light-slot selftest: FAILED -- guest light allocation "
               "failed.\n");
        return 1;
    }
    light = (float *)(uintptr_t)light_address;
    memset(light, 0, (26u + 3u) * sizeof(uint32_t));
    ((uint32_t *)light)[0] = 3u;                       /* directional */
    light[1] = 0.25f;
    light[2] = 0.50f;
    light[3] = 0.75f;
    light[4] = 1.00f;
    light[18] = 1.0f;
    light[19] = 1000.0f;

    args[0] = CONTROL_LIGHT_INDEX;
    args[1] = light_address;
    result = call(device, 44, args, 2);                /* SetLight */
    if (result != D3D_OK ||
        !d3d8_last_setlight_diffuse(CONTROL_LIGHT_INDEX, retained) ||
        retained[0] != light[1] || retained[1] != light[2] ||
        retained[2] != light[3]) {
        printf("d3d8 light-slot selftest: FAILED -- SetLight(51) returned "
               "0x%08x or did not retain diffuse %.2f %.2f %.2f.\n",
               result, light[1], light[2], light[3]);
        fails++;
    }

    args[0] = CONTROL_LIGHT_INDEX;
    args[1] = 1;
    result = call(device, 46, args, 2);                /* LightEnable */
    if (result != D3D_OK) {
        printf("d3d8 light-slot selftest: FAILED -- LightEnable(51, TRUE) "
               "returned 0x%08x, not D3D_OK.\n", result);
        fails++;
    }

    printf("d3d8 light-slot selftest: %s\n", fails ? "FAILED"
           : "PASSED -- SetLight and LightEnable accepted stock slot 51 and "
             "the draw-time witness retained its colour");
    return fails;
}
