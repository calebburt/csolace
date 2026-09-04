#ifndef SLC_VALUE_H
#define SLC_VALUE_H

#include "dynamic_array.h"
#include "type.h"

typedef struct VM VM;

typedef enum {
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_FUNCTION,
    OBJ_STRING,
    OBJ_UPVALUE,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_PROTOTYPE,
} ObjType;

typedef struct Obj { // fix forward declaration
    // type needs only 3 bits and isMarked 1; packing them into one 4-byte word
    // byte would force 7 bytes of padding before the pointers, pushing it to 32.
    ObjType type : 8;
    bool isMarked : 1;
    uint32_t hash;
    struct Obj *class;
    struct Obj *next;
} Obj;

typedef struct ObjString {
    Obj obj;
    int length;
    char *chars;
} ObjString;

typedef struct {
    Obj obj;
    int fieldCount;
    Value *fields;
} ObjInstance;

// ObjPrototype is defined in chunk.h (it embeds a Chunk by value).
typedef struct ObjPrototype ObjPrototype;

typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ,
} ValueType;

typedef struct Value {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj *obj;
    } as;
} Value;

MAKE_DYNAMIC_ARRAY_H(Value, ValueArray)

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

typedef struct {
    Obj obj;
    Value receiver;
    ObjFunction* method;
} ObjBoundMethod;

typedef struct {
    Obj obj;
    ObjString *name; 
    int fieldCount;
    ValueArray methods;
} ObjClass;

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

#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value) isObjType(value, OBJ_CLASS)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_PROTOTYPE(value) isObjType(value, OBJ_PROTOTYPE)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)

#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass*)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (AS_STRING(value)->chars)
#define AS_PROTOTYPE(value) ((ObjPrototype*)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value) ((ObjNative*)AS_OBJ(value))

#define GET_CLASS(value) ((ObjClass*)AS_OBJ(value)->class)


void printValue(Value value);
bool valuesEqual(Value a, Value b);
uint32_t hashValue(Value value);

ObjBoundMethod* newBoundMethod(VM *vm, Value receiver, ObjFunction* method);
ObjClass *newClass(VM *vm, ObjString *name, int fieldCount);
ObjFunction *newFunction(VM *vm, ObjPrototype *prototype);
ObjPrototype *newPrototype(VM *vm);
ObjString *copyString(VM *vm, const char *chars, int length);
ObjUpvalue *newUpvalue(VM *vm, Value *slot);
ObjString *allocateString(VM *vm, char *chars, int length);
ObjInstance *newInstance(VM *vm, ObjClass *class);
ObjNative *newNative(VM *vm, NativeFn function, const char *name);

void freeObject(VM *vm, Obj *object);
void freeObjects(VM *vm, Obj *objects);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif