#include "install_picker.h"
#include "install_requirements.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int create_file(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static int create_parent_directories(char *path)
{
    char *cursor;
    for (cursor = path; *cursor; ++cursor) {
        if (*cursor != '/') continue;
        *cursor = 0;
        if (mkdir(path, 0700) != 0 && errno != EEXIST) return 0;
        *cursor = '/';
    }
    return 1;
}

static void remove_empty_parent_directories(char *path, size_t root_length)
{
    char *slash;
    while ((slash = strrchr(path, '/')) != NULL && (size_t)(slash - path) > root_length) {
        *slash = 0;
        rmdir(path);
    }
}

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
        snprintf(path, sizeof path, "%s/%s", root, x2_install_required_images[i]);
        if (!create_file(path)) {
            perror("create install picker fixture");
            return 1;
        }
    }
    for (i = 0; i < X2_INSTALL_REQUIRED_CONTENT_COUNT; ++i) {
        snprintf(path, sizeof path, "%s/%s", root, x2_install_required_content[i]);
        if (!create_parent_directories(path) || !create_file(path)) {
            perror("create install content fixture");
            return 1;
        }
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

    snprintf(path, sizeof path, "%s/%s", root, x2_install_required_content[0]);
    if (remove(path) != 0) {
        perror("remove required content fixture");
        return 1;
    }
    if (x2_install_picker_prepare_selection(root, "", reason, sizeof reason)) {
        fprintf(stderr, "loader-only install was accepted\n");
        return 1;
    }
    if (!create_file(path)) {
        perror("restore required content fixture");
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
    for (i = 0; i < X2_INSTALL_REQUIRED_CONTENT_COUNT; ++i) {
        snprintf(path, sizeof path, "%s/%s", root, x2_install_required_content[i]);
        remove(path);
        remove_empty_parent_directories(path, strlen(root));
    }
    rmdir(root);
    puts("install_picker: complete-install validation passed");
    return 0;
}
