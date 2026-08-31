#include "install_picker.h"
#include "install_requirements.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    const char root[] = "install-picker-test";
    char path[4096], directory[4096], reason[512];
    unsigned i;

    if (mkdir(root, 0700) != 0 && errno != EEXIST) {
        perror("mkdir install picker fixture");
        return 1;
    }
    for (i = 0; i < X2_INSTALL_REQUIRED_IMAGE_COUNT; ++i) {
        FILE *file;
        snprintf(path, sizeof path, "%s/%s", root, x2_install_required_images[i]);
        file = fopen(path, "wb");
        if (!file) {
            perror("create install picker fixture");
            return 1;
        }
        fclose(file);
    }

    snprintf(path, sizeof path, "%s/XMen2.exe", root);
    if (!x2_install_picker_directory_from_executable(
            path, directory, sizeof directory) || strcmp(directory, root) != 0) {
        fprintf(stderr, "complete XMen2.exe fixture was refused\n");
        return 1;
    }
    if (!x2_install_picker_prepare_selection(root, "", reason, sizeof reason)) {
        fprintf(stderr, "complete install folder was refused: %s\n", reason);
        return 1;
    }

    snprintf(path, sizeof path, "%s/%s", root, x2_install_required_images[1]);
    if (remove(path) != 0) {
        perror("remove required install fixture");
        return 1;
    }
    snprintf(path, sizeof path, "%s/XMen2.exe", root);
    if (x2_install_picker_directory_from_executable(path, directory,
                                                    sizeof directory) ||
        x2_install_picker_prepare_selection(root, "", reason, sizeof reason)) {
        fprintf(stderr, "incomplete install was accepted\n");
        return 1;
    }

    for (i = 0; i < X2_INSTALL_REQUIRED_IMAGE_COUNT; ++i) {
        snprintf(path, sizeof path, "%s/%s", root, x2_install_required_images[i]);
        remove(path);
    }
    rmdir(root);
    puts("install_picker: complete-install validation passed");
    return 0;
}
