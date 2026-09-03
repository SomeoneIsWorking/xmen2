#include "guest_modules.h"

#include "x86rt_native.h"
#include "install_requirements.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * One X86Module per image, with storage that outlives the call: the dispatcher
 * keeps the pointers on a list rather than copying them.
 *
 * `base` is filled in by the mapping loop and `preferred` by the PE header it
 * reads there. Both are zero here on purpose -- a module that was never mapped
 * must not look like one mapped at address 0, which is a real guest address.
 */
static X86Module g_module[X2_INSTALL_REQUIRED_IMAGE_COUNT];
static uint32_t  g_base[X2_INSTALL_REQUIRED_IMAGE_COUNT];

int guest_modules_register(void)
{
    unsigned i;
    for (i = 0; i < X2_INSTALL_REQUIRED_IMAGE_COUNT; i++) {
        const char *name = x2_install_required_images[i];
        if (!name || !*name) {
            fprintf(stderr, "guest_modules: entry %u of the required-image "
                            "list is empty, so the inventory would be short by "
                            "one module and nothing would say which\n", i);
            return 1;
        }
        g_module[i].name = name;
        g_module[i].base = &g_base[i];
        x86_module_register(&g_module[i]);
    }
    return 0;
}
