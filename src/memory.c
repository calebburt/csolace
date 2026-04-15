#include "memory.h"
#include "debug.h"

size_t bytesAllocated = 0;

void *reallocate(void *pointer, size_t oldSize, size_t newSize) {
    bytesAllocated += newSize - oldSize;
    debug("Allocating %d bytes ", newSize - oldSize);
    debug("%lu bytes allocated in total so far.\n", bytesAllocated);

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    return realloc(pointer, newSize);
}
