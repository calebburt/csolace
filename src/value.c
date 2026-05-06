#include "common.h"
#include "value.h"
#include "vm.h"
#include "memory.h"

MAKE_DYNAMIC_ARRAY(Value, ValueArray)

static void printFunction(ObjPrototype *function) {
    if (function->name == NULL) {
        printf("<Function script>");
        return;
    }
    printf("<Function %s>", function->name->chars);
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_PROTOTYPE:
            printFunction(AS_PROTOTYPE(value));
            break;
    }
}

void printValue(Value value) {
    switch (value.type) {
        case VAL_BOOL: printf(AS_BOOL(value) ? "true" : "false"); break;
        case VAL_NIL: printf("nil"); break;
        case VAL_NUMBER: printf("%g", AS_NUMBER(value)); break;
        case VAL_OBJ: printObject(value); break;
    }
}

bool valuesEqual(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL: return true;
        case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
        case VAL_OBJ: {
            ObjString *aString = AS_STRING(a);
            ObjString *bString = AS_STRING(b);
            return aString->length == bString->length && memcmp(aString->chars, bString->chars, aString->length) == 0;
        }
        default: return false;
    }
}

uint32_t hashString(ObjString *string) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < string->length; i++) {
        hash ^= (uint8_t)string->chars[i];
        hash *= 16777619;
    }
    return hash;
}

uint32_t hashValue(Value value) {
    switch (value.type) {
        case VAL_BOOL: return (uint32_t)value.as.boolean;
        case VAL_NIL: return (uint32_t)3;
        case VAL_NUMBER: {
            double num = AS_NUMBER(value);
            uint64_t bits;
            memcpy(&bits, &num, sizeof(bits));
            return (uint32_t)(bits ^ (bits >> 32));
        }
        case VAL_OBJ: {
            if (AS_OBJ(value)->hash != 0) return AS_OBJ(value)->hash;
            switch(OBJ_TYPE(value)) {
                case OBJ_STRING:
                    return hashString(AS_STRING(value));
                default: return 0;
            }
        }
        default: return 0;
    }
}

#define ALLOCATE_OBJ(vm, type, objectType) (type*)allocateObject(vm, sizeof(type), objectType)

static Obj* allocateObject(VM *vm, size_t size, ObjType type) {
    Obj *object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->next = vm->objects;
    object->hash = 0;
    vm->objects = object;
    return object;
}

ObjPrototype *newPrototype(VM *vm) {
    ObjPrototype *prototype = ALLOCATE_OBJ(vm, ObjPrototype, OBJ_PROTOTYPE);
    initTypeArray(&prototype->paramaters);
    prototype->name = NULL;
    initChunk(prototype->chunk);
    return prototype;
}

ObjString *allocateString(VM *vm, char *chars, int length) {
    ObjString *string = ALLOCATE_OBJ(vm, ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->obj.hash = hashValue(OBJ_VAL(string));
    return string;
}

ObjString* copyString(VM *vm, const char *chars, int length) {
    char *heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(vm, heapChars, length);
}

void freeObject(Obj *object) {
    switch (object->type) {
        case OBJ_STRING: {
            ObjString *string = (ObjString*)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(ObjString, object);
            break;
        }
        case OBJ_PROTOTYPE: {
            ObjPrototype *prototype = (ObjPrototype*)object;
            freeChunk(prototype->chunk);
            freeTypeArray(&prototype->paramaters);
            FREE(ObjPrototype, object);
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
