#ifndef SLC_VALUE_H
#define SLC_VALUE_H

#include "dynamic_array.h"
#include "type.h"

typedef struct VM VM;

typedef enum {
    OBJ_FUNCTION,
    OBJ_STRING,
    OBJ_UPVALUE,
    OBJ_NATIVE,
    OBJ_PROTOTYPE,
} ObjType;

typedef struct Obj { // fix forward declaration
    ObjType type;
    uint32_t hash;
    struct Obj *next;
} Obj;

typedef struct ObjString {
    Obj obj;
    int length;
    char *chars;
} ObjString;

// ObjPrototype is defined in chunk.h (it embeds a Chunk by value).
typedef struct ObjPrototype ObjPrototype;

typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj *obj;
    } as;
} Value;

// NativeFn must come after Value since it references Value by name.
typedef bool (*NativeFn)(VM *vm, int argCount, Value *args, Value *out);

typedef struct ObjNative {
    Obj obj;
    NativeFn function;
    const char *name;
} ObjNative;

typedef struct ObjUpvalue {
    Obj obj;
    Value *location;
    struct ObjUpvalue *next;
    Value closed;
} ObjUpvalue;

typedef struct {
    Obj obj;
    ObjPrototype *prototype;
    ObjUpvalue **upvalues;
    int upvalueCount;
} ObjFunction;

static inline bool isObjType(Value value, ObjType type);


#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value) ((value).type == VAL_OBJ)

#define AS_OBJ(value) ((value).as.obj)
#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)

#define BOOL_VAL(value) ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object) ((Value){VAL_OBJ, {.obj = (Obj*)object}})

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_PROTOTYPE(value) isObjType(value, OBJ_PROTOTYPE)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)

#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (AS_STRING(value)->chars)
#define AS_PROTOTYPE(value) ((ObjPrototype*)AS_OBJ(value))
#define AS_NATIVE(value) ((ObjNative*)AS_OBJ(value))


MAKE_DYNAMIC_ARRAY_H(Value, ValueArray)

void printValue(Value value);
bool valuesEqual(Value a, Value b);
uint32_t hashValue(Value value);

ObjFunction *newFunction(VM *vm, ObjPrototype *prototype);
ObjPrototype *newPrototype(VM *vm);
ObjString *copyString(VM *vm, const char *chars, int length);
ObjUpvalue *newUpvalue(VM *vm, Value *slot);
ObjString *allocateString(VM *vm, char *chars, int length);
ObjNative *newNative(VM *vm, NativeFn function, const char *name);

void freeObject(Obj *object);
void freeObjects(Obj *objects);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif