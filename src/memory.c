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

static void freeObject(Obj *object) {
    switch (object->type) {
        case OBJ_STRING: {
            ObjString *string = (ObjString*)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(ObjString, object);
            break;
        }
    }
}

void freeObjects(Obj *objects) {
    Obj *object = objects;
    while (object != NULL) {
        Obj *next = object->next;
        freeObject(object);
        object = next;
    }
}
