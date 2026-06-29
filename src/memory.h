#ifndef SLC_MEMORY_H
#define SLC_MEMORY_H

#include "common.h"

typedef struct VM VM;
typedef struct Obj Obj;
typedef struct Value Value;
typedef struct Type Type;

#define ALLOCATE(vm, type, count) (type*)reallocate(vm, NULL, 0, sizeof(type) * (count))
#define FREE(vm, type, pointer) reallocate(vm, pointer, sizeof(type), 0)

void *reallocate(VM *vm, void *pointer, size_t oldSize, size_t newSize);

void markObject(VM *vm, Obj *object);
void markValue(VM *vm, Value value);
void markType(VM *vm, Type *type);

void collectGarbage(VM *vm);

#endif