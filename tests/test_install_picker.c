#include "install_picker.h"

#include <stdio.h>
#include <string.h>

#ifndef X2_INSTALL_PICKER_FIXTURE
#error "X2_INSTALL_PICKER_FIXTURE must name the non-game test fixture"
#endif

int main(void)
{
    char directory[4096];
    const char *fixture = X2_INSTALL_PICKER_FIXTURE;
    const char *slash = strrchr(fixture, '/');
    size_t length = slash ? (size_t)(slash - fixture) : 0;

    if (!slash || !length || length >= sizeof directory
            || !x2_install_picker_directory_from_executable(
                fixture, directory, sizeof directory)) {
        fprintf(stderr, "valid XMen2.exe fixture was refused\n");
        return 1;
    }
    if (strncmp(directory, fixture, length) != 0 || directory[length] != 0) {
        fprintf(stderr, "fixture directory was parsed incorrectly: %s\n", directory);
        return 1;
    }
    if (x2_install_picker_directory_from_executable(
            "/does/not/exist/XMen2.exe", directory, sizeof directory)) {
        fprintf(stderr, "missing executable was accepted\n");
        return 1;
    }
    puts("install_picker: validation passed");
    return 0;
}
