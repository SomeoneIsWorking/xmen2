#ifndef X2NATIVE_OPTIONS_H
#define X2NATIVE_OPTIONS_H

typedef struct {
    const char *install_dir;
    /* Non-NULL records the exact DirectInput snapshots. Empty = unique path. */
    const char *input_record;
    int window;
    /* --unbounded: skip the scheduler's idle waits instead of sleeping
       through them. Removes real seconds, never guest work -- see
       guest_clock.h. */
    int unbounded;
    /* --control[=port]: the live control channel, 0 = off. See control.h. */
    int control;
    /* The implicit D3D8+run route, whose supporting live tools are defaults. */
    int product;
    int appimage;
    int selftest;
    int run;
    int ark_probe;
    int vk;
    int vk_selftest;
    int vk_permissive;
    int d3d8;
    int d3d8_selftest;
    int d3d8_permissive;
    int dialog_selftest;
    int override_selftest;
    int fault_selftest;
} X2NativeOptions;

int x2native_options_parse(int argc, char **argv, X2NativeOptions *options);
/* Developer launches may use the checkout's .env. Packaged setup never may. */
int x2native_options_uses_project_env(const X2NativeOptions *options);

#endif /* X2NATIVE_OPTIONS_H */
