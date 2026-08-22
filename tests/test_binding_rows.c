#include "binding_rows.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(c) do { assert(c); checks++; } while (0)

static void check_row(uint32_t row, const char *key, const char *label)
{
    CHECK(strcmp(input_binding_row_storage_key(row), key) == 0);
    CHECK(strcmp(input_binding_row_display_label(row), label) == 0);
}

int main(void)
{
    uint32_t row;

    for (row = 0; row < INPUT_BINDING_ROWS; row++) {
        CHECK(input_binding_row_storage_key(row) != NULL);
        CHECK(input_binding_row_display_label(row) != NULL);
    }

    /* These are deliberate semantic discriminators: the old UI exposed the
       left-hand persistence identifiers, which describe neither consumable. */
    check_row(9, "Ally", "Energy Pack");
    check_row(10, "TargetLock", "Health Pack");
    check_row(23, "SreenGrab", "Screenshot");
    CHECK(input_binding_row_storage_key(INPUT_BINDING_ROWS) == NULL);
    CHECK(input_binding_row_display_label(INPUT_BINDING_ROWS) == NULL);
    CHECK(input_binding_row_storage_key(UINT32_MAX) == NULL);
    CHECK(input_binding_row_display_label(UINT32_MAX) == NULL);

    printf("test_binding_rows: %d checks over %u binding rows\n", checks,
           INPUT_BINDING_ROWS);
    return 0;
}
