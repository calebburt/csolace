#include "common.h"
#include "value.h"
#include "vm.h"
#include "memory.h"
#include "debug.h"

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
        case OBJ_CLASS: {
            printf("<Class %s>", AS_CLASS(value)->name->chars);
            break;
        }
        case OBJ_FUNCTION:
            printFunction(AS_FUNCTION(value)->prototype);
            break;
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_UPVALUE:
            printf("<upvalue>");
            break;
        case OBJ_INSTANCE:
            printf("<%s>", GET_CLASS(value)->name->chars);
            break;
        case OBJ_NATIVE:
            printf("<Function %s>", AS_NATIVE(value)->name);
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
        case VAL_NUMBER: printf("%.17g", AS_NUMBER(value)); break;
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

static const char *objTypeName(ObjType type) {
    switch (type) {
        case OBJ_FUNCTION: return "OBJ_FUNCTION";
        case OBJ_STRING: return "OBJ_STRING";
        case OBJ_UPVALUE: return "OBJ_UPVALUE";
        case OBJ_NATIVE: return "OBJ_NATIVE";
        case OBJ_PROTOTYPE: return "OBJ_PROTOTYPE";
        default: return "OBJ_UNKNOWN";
    }
}

static Obj* allocateObject(VM *vm, size_t size, ObjType type) {
    Obj *object = (Obj*)reallocate(vm, NULL, 0, size);
    object->type = type;
    object->next = vm->objects;
    object->hash = 0;
    object->isMarked = false;
    object->class = NULL;
    vm->objects = object;
    debug("%p allocate %zu for %s\n", (void*)object, size, objTypeName(type));
    return object;
}

ObjClass *newClass(VM *vm, ObjString *name) {
    ObjClass *class = ALLOCATE_OBJ(vm, ObjClass, OBJ_CLASS);
    class->name = name;
    return class;
}

ObjFunction *newFunction(VM *vm, ObjPrototype *prototype) {
    ObjUpvalue **upvalues = ALLOCATE(vm, ObjUpvalue*, prototype->upvalueCount);
    for (int i = 0; i < prototype->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    ObjFunction *function = ALLOCATE_OBJ(vm, ObjFunction, OBJ_FUNCTION);
    function->prototype = prototype;
    function->upvalues = upvalues;
    function->upvalueCount = prototype->upvalueCount;
    return function;
}

ObjPrototype *newPrototype(VM *vm) {
    ObjPrototype *prototype = ALLOCATE_OBJ(vm, ObjPrototype, OBJ_PROTOTYPE);
    initTypeArray(&prototype->paramaters);
    prototype->name = NULL;
    prototype->upvalueCount = 0;
    initChunk(&prototype->chunk);
    return prototype;
}

ObjNative *newNative(VM *vm, NativeFn function, const char *name) {
    ObjNative *native = ALLOCATE_OBJ(vm, ObjNative, OBJ_NATIVE);
    native->function = function;
    native->name = name;
    return native;
}

ObjString *allocateString(VM *vm, char *chars, int length) {
    ObjString *string = ALLOCATE_OBJ(vm, ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->obj.hash = hashValue(OBJ_VAL(string));
    return string;
}

ObjString* copyString(VM *vm, const char *chars, int length) {
    char *heapChars = ALLOCATE(vm, char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(vm, heapChars, length);
}

ObjUpvalue *newUpvalue(VM *vm, Value *slot) {
    ObjUpvalue *upvalue = ALLOCATE_OBJ(vm, ObjUpvalue, OBJ_UPVALUE);
    upvalue->closed = NIL_VAL;
    upvalue->location = slot;
    upvalue->next = NULL;
    return upvalue;
}

ObjInstance *newInstance(VM *vm, ObjClass *class, int fieldCount) {
    ObjInstance *instance = ALLOCATE_OBJ(vm, ObjInstance, OBJ_INSTANCE);
    instance->obj.class = (Obj*)class;
    instance->fieldCount = fieldCount;
    instance->fields = ALLOCATE(vm, Value, fieldCount);
    for (int i = 0; i < fieldCount; i++) {
        instance->fields[i] = NIL_VAL;
    }
    return instance;
}

void freeObject(VM *vm, Obj *object) {
    debug("%p free type %s\n", (void*)object, objTypeName(object->type));
    switch (object->type) {
        case OBJ_CLASS: {
            FREE(vm, ObjClass, object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction *function = (ObjFunction*)object;
            FREE_ARRAY(vm, ObjUpvalue*, function->upvalues, function->upvalueCount);
            FREE(vm, OBJ_FUNCTION, object);
            break;
        }
        case OBJ_STRING: {
            ObjString *string = (ObjString*)object;
            FREE_ARRAY(vm, char, string->chars, string->length + 1);
            FREE(vm, ObjString, object);
            break;
        }
        case OBJ_UPVALUE:
            FREE(vm, ObjUpvalue, object);
            break;
        case OBJ_NATIVE:
            FREE(vm, ObjNative, object);
            break;
        case OBJ_INSTANCE:
            reallocate(vm, ((ObjInstance*)object)->fields, sizeof(Value)*((ObjInstance*)object)->fieldCount, 0);
            FREE(vm, ObjInstance, object);
            break;
        case OBJ_PROTOTYPE: {
            ObjPrototype *prototype = (ObjPrototype*)object;
            freeChunk(vm, &prototype->chunk);
            freeTypeArray(vm, &prototype->paramaters);
            FREE(vm, ObjPrototype, object);
            break;
        }
    }
}

void freeObjects(VM *vm, Obj *objects) {
    Obj *object = objects;
    while (object != NULL) {
        Obj *next = object->next;
        freeObject(vm, object);
        object = next;
    }

    free(vm->grayStack);
}
