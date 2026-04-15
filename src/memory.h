#ifndef SLC_MEMORY_H
#define SLC_MEMORY_H

#include "common.h"

extern struct Obj;

#define ALLOCATE(type, count) (type*)reallocate(NULL, 0, sizeof(type) * (count))
#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

void *reallocate(void *pointer, size_t oldSize, size_t newSize);
void freeObjects(struct Obj *objects);

#endif