#include "common.h"
#include "value.h"
#include "vm.h"
#include "memory.h"

MAKE_DYNAMIC_ARRAY(Value, ValueArray)

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
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

#define ALLOCATE_OBJ(vm, type, objectType) (type*)allocateObject(vm, sizeof(type), objectType)

static Obj* allocateObject(VM *vm, size_t size, ObjType type) {
    Obj *object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->next = vm->objects;
    vm->objects = object;
    return object;
}

ObjString *allocateString(VM *vm, char *chars, int length) {
    ObjString *string = ALLOCATE_OBJ(vm, ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    return string;
}

ObjString* copyString(VM *vm, const char *chars, int length) {
    char *heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(vm, heapChars, length);
}
