#ifndef X2_CONTROL_STATUS_H
#define X2_CONTROL_STATUS_H

#include <stddef.h>

size_t control_status_format(char *body, size_t capacity,
                             unsigned long requests,
                             unsigned long keys_pressed,
                             unsigned long keys_refused,
                             unsigned long screenshots);

#endif /* X2_CONTROL_STATUS_H */
