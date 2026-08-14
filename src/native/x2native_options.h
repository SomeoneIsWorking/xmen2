#ifndef X2NATIVE_OPTIONS_H
#define X2NATIVE_OPTIONS_H

typedef struct {
    const char *install_dir;
    int window;
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
    int fault_selftest;
} X2NativeOptions;

int x2native_options_parse(int argc, char **argv, X2NativeOptions *options);

#endif /* X2NATIVE_OPTIONS_H */
