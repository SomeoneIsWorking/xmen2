#include "control_query.h"

#include <string.h>
#include <stdlib.h>

int control_query_arg(const char *query, const char *name,
                      char *out, size_t out_size)
{
    size_t name_size;
    const char *part;

    if (!query || !name || !out || !out_size) return 0;
    name_size = strlen(name);
    part = query;
    while (*part) {
        if (!strncmp(part, name, name_size) && part[name_size] == '=') {
            const char *value = part + name_size + 1;
            const char *end = strchr(value, '&');
            size_t size = end ? (size_t)(end - value) : strlen(value);
            if (size >= out_size) size = out_size - 1;
            memcpy(out, value, size);
            out[size] = '\0';
            return 1;
        }
        part = strchr(part, '&');
        if (!part) break;
        part++;
    }
    return 0;
}

int bounded_number(const char *text, int minimum, int maximum, int *out)
{
    char *end;
    long value;
    if (!text || !*text) return 0;
    value = strtol(text, &end, 10);
    if (*end || value < minimum || value > maximum) return 0;
    *out = (int)value;
    return 1;
}
