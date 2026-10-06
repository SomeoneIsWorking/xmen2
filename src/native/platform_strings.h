#ifndef X2_PLATFORM_STRINGS_H
#define X2_PLATFORM_STRINGS_H

#if defined(_WIN32)
#include <string.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#endif
