#include "memory.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"

size_t bytesAllocated = 0;

// How much the heap is allowed to grow between collections.
#define GC_HEAP_GROW_FACTOR 2

void *reallocate(VM *vm, void *pointer, size_t oldSize, size_t newSize) {
    bytesAllocated += newSize - oldSize;

    // canGC stays false during init and compilation (see VM::canGC). Only growing
    // allocations can push us over the threshold, so we never collect on a shrink.
    if (newSize > oldSize && vm->canGC) {
        #ifdef SLC_DEBUG
            collectGarbage(vm);
        #endif
        if (bytesAllocated > vm->nextGC) {
            collectGarbage(vm);
        }
    }

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    return realloc(pointer, newSize);
}

// Garbage collector!

// helpers
void markObject(VM *vm, Obj *object) {
    if (object == NULL) return;
    if (object->isMarked) return;

    debug("%p mark ", (void*)object);
    #ifdef SLC_DEBUG
    printValue(OBJ_VAL(object));
    #endif
    debug("\n");

    object->isMarked = true;

    if (vm->grayCapacity < vm->grayCount + 1) {
        vm->grayCapacity = GROW_CAPACITY(vm->grayCapacity);
        vm->grayStack = (Obj**)realloc(vm->grayStack, sizeof(Obj*) * vm->grayCapacity);
        if (vm->grayStack == NULL) exit(1);
    }

    vm->grayStack[vm->grayCount++] = object;
}

void markValue(VM *vm, Value value) {
    if (IS_OBJ(value)) markObject(vm, AS_OBJ(value));
}

static void markArray(VM *vm, ValueArray *array) {
    for (int i = 0; i < array->count; i++) {
        markValue(vm, array->data[i]);
    }
}

static void blackenObject(VM *vm, Obj *object) {
    debug("%p blacken ", (void*)object);
    #ifdef SLC_DEBUG
    printValue(OBJ_VAL(object));
    #endif
    debug("\n");

    switch (object->type) {
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
        case OBJ_UPVALUE:
            markValue(vm, ((ObjUpvalue*)object)->closed);
            break;
        case OBJ_PROTOTYPE: {
            ObjPrototype *prototype = (ObjPrototype*)object;
            markObject(vm, (Obj*)prototype->name);
            markArray(vm, &prototype->chunk.constants);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction *function = (ObjFunction*)object;
            markObject(vm, (Obj*)function->prototype);
            for (int i = 0; i < function->upvalueCount; i++) {
                markObject(vm, (Obj*)function->upvalues[i]);
            }
            break;
        }
    }
}

// high level ops
static void markRoots(VM *vm) {
    for (Value *slot = vm->stack; slot < vm->stackTop; slot++) {
        markValue(vm, *slot);
    }

    for (int i = 0; i < vm->frameCount; i++) {
        markObject(vm, (Obj*)vm->frames[i].function);
    }

    for (ObjUpvalue *upvalue = vm->openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject(vm, (Obj*)upvalue);
    }

    for (int i = 0; i < vm->nativeCount; i++) {
        markObject(vm, (Obj*)vm->natives[i]);
    }

    markCompilerRoots(vm);
}

static void traceReferences(VM *vm) {
    while (vm->grayCount > 0) {
        Obj *object = vm->grayStack[--vm->grayCount];
        blackenObject(vm, object);
    }
}

static void sweep(VM *vm) {
    Obj *previous = NULL;
    Obj *object = vm->objects;
    while (object != NULL) {
        if (object->isMarked) {
            object->isMarked = false;
            previous = object;
            object = object->next;
        } else {
            Obj *unreached = object;
            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm->objects = object;
            }

            freeObject(vm, unreached);
        }
    }
}

void collectGarbage(VM *vm) {
    debug("-- gc begin\n");
    size_t before = bytesAllocated;

    markRoots(vm);
    traceReferences(vm);
    sweep(vm);

    vm->nextGC = bytesAllocated * GC_HEAP_GROW_FACTOR;

    debug("-- gc end\n");
    debug("   collected %zu bytes (from %zu to %zu) next at %zu\n",
          before - bytesAllocated, before, bytesAllocated, vm->nextGC);
}
