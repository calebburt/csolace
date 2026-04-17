#ifndef SLC_DYNAMIC_ARRAY_H
#define SLC_DYNAMIC_ARRAY_H

#include "common.h"
#include "memory.h"

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

#define MAKE_DYNAMIC_ARRAY(type, name) \
    void init##name(name *array) { \
        array->data = NULL; \
        array->count = 0; \
        array->capacity = 0; \
    } \
    \
    void append##name(name *array, type value) { \
        if (array->capacity < array->count + 1) { \
            int oldCapacity = array->capacity; \
            array->capacity = GROW_CAPACITY(oldCapacity) ;\
            array->data = GROW_ARRAY(type, array->data, oldCapacity, array->capacity); \
        } \
         \
        array->data[array->count] = value; \
        array->count++; \
    } \
    \
    void free##name(name *array) { \
        FREE_ARRAY(type, array->data, array->capacity); \
        init##name(array); \
    }

#define MAKE_DYNAMIC_ARRAY_H(type, name) \
    typedef struct { \
        type *data; \
        int count; \
        int capacity; \
    } name; \
    \
    void init##name(name *array); \
    void append##name(name *array, type value); \
    void free##name(name *array);

#endif