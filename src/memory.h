#ifndef SLC_MEMORY_H
#define SLC_MEMORY_H

#include "common.h"

#define ALLOCATE(type, count) (type*)reallocate(NULL, 0, sizeof(type) * (count))

void *reallocate(void *pointer, size_t oldSize, size_t newSize);

#endif