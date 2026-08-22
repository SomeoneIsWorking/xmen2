#define _POSIX_C_SOURCE 200809L

#include "save_catalog.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char test_dir[] = "scratch/save-catalog-test-XXXXXX";
static int checks;

#define CHECK(c) do { assert(c); checks++; } while (0)

static void path_for(char *path, size_t capacity, const char *leaf)
{
    int length = snprintf(path, capacity, "%s/%s", test_dir, leaf);
    CHECK(length > 0 && (size_t)length < capacity);
}

static void clear_test_dir(void)
{
    struct dirent *entry;
    struct stat st;
    char path[256];
    DIR *dir = opendir(test_dir);

    if (!dir) return;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        path_for(path, sizeof path, entry->d_name);
        CHECK(lstat(path, &st) == 0);
        if (S_ISDIR(st.st_mode)) CHECK(rmdir(path) == 0);
        else CHECK(unlink(path) == 0);
    }
    CHECK(closedir(dir) == 0);
}

static void cleanup(void)
{
    if (!test_dir[0]) return;
    clear_test_dir();
    CHECK(rmdir(test_dir) == 0);
    test_dir[0] = '\0';
}

static void write_file(const char *leaf, const char *contents)
{
    char path[256];
    FILE *file;

    path_for(path, sizeof path, leaf);
    file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fputs(contents, file) >= 0);
    CHECK(fclose(file) == 0);
}

static void set_mtime(const char *leaf, int64_t seconds, long nanoseconds)
{
    struct timespec times[2];
    char path[256];

    path_for(path, sizeof path, leaf);
    times[0].tv_sec = (time_t)seconds;
    times[0].tv_nsec = nanoseconds;
    times[1] = times[0];
    CHECK(utimensat(AT_FDCWD, path, times, 0) == 0);
}

static X2SaveCandidate latest(void)
{
    X2SaveCandidate candidate;
    CHECK(x2_save_catalog_latest(test_dir, &candidate) == 1);
    return candidate;
}

static void test_exact_regular_leaves(void)
{
    X2SaveCandidate candidate;
    char path[256];

    write_file("saveslot0.save.new", "partial");
    write_file("saveslot10.save", "out of range");
    write_file("saveslotx.save", "malformed");
    write_file("Saveslot0.save", "wrong case");
    write_file("autosave.save.new", "partial autosave");
    write_file("other.save", "unrelated");
    path_for(path, sizeof path, "saveslot1.save");
    CHECK(mkdir(path, 0700) == 0);
    path_for(path, sizeof path, "saveslot2.save");
    CHECK(symlink("other.save", path) == 0);

    CHECK(x2_save_catalog_latest(test_dir, &candidate) == 0);
    clear_test_dir();
}

static void test_sparse_slots_and_nanoseconds(void)
{
    X2SaveCandidate candidate;

    write_file("saveslot0.save", "slot zero");
    write_file("saveslot4.save", "slot four");
    write_file("saveslot9.save", "slot nine");
    write_file("autosave.save", "autosave");
    set_mtime("saveslot0.save", 10, 900);
    set_mtime("saveslot4.save", 20, 100);
    set_mtime("saveslot9.save", 20, 101);
    set_mtime("autosave.save", 19, 999999999);

    candidate = latest();
    CHECK(!strcmp(candidate.leaf, "saveslot9.save"));
    CHECK(candidate.mtime_ns == INT64_C(20000000101));
    clear_test_dir();
}

static void test_equal_time_tie_is_by_leaf(void)
{
    X2SaveCandidate candidate;

    write_file("autosave.save", "auto");
    write_file("saveslot2.save", "two");
    write_file("saveslot9.save", "nine");
    set_mtime("autosave.save", 30, 77);
    set_mtime("saveslot2.save", 30, 77);
    set_mtime("saveslot9.save", 30, 77);

    candidate = latest();
    CHECK(!strcmp(candidate.leaf, "saveslot9.save"));
    clear_test_dir();
}

static void test_newest_corrupt_file_is_opaque(void)
{
    X2SaveCandidate candidate;

    write_file("saveslot3.save", "plausible old save");
    write_file("autosave.save", "not a valid game save");
    set_mtime("saveslot3.save", 40, 0);
    set_mtime("autosave.save", 41, 0);

    candidate = latest();
    CHECK(!strcmp(candidate.leaf, "autosave.save"));
    clear_test_dir();
}

int main(void)
{
    X2SaveCandidate candidate;

    if (mkdir("scratch", 0700) != 0 && errno != EEXIST) return 2;
    if (!mkdtemp(test_dir)) return 2;
    atexit(cleanup);

    CHECK(x2_save_catalog_latest(NULL, &candidate) == -1);
    CHECK(x2_save_catalog_latest(test_dir, NULL) == -1);
    test_exact_regular_leaves();
    test_sparse_slots_and_nanoseconds();
    test_equal_time_tie_is_by_leaf();
    test_newest_corrupt_file_is_opaque();

    cleanup();
    printf("save_catalog: %d checks passed\n", checks);
    return 0;
}
