#ifndef __ASSETS__
#define __ASSETS__

#include <stddef.h>
#include <stdint.h>

typedef struct blob_s {
    uint8_t *bytes; /* W_ReadFile appends a NUL byte outside size. */
    size_t size;
} blob_t;

#endif
