#ifndef SLC_VALUE_H
#define SLC_VALUE_H

#include "dynamic_array.h"

struct VM;

typedef enum {
    OBJ_STRING,
} ObjType;

typedef struct Obj { // fix forward declaration
    ObjType type;
    struct Obj *next;
} Obj;

typedef struct {
    Obj obj;
    int length;
    char *chars;
} ObjString;

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

#define IS_STRING(value) isObjType(value, OBJ_STRING)

#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (AS_STRING(value)->chars)


MAKE_DYNAMIC_ARRAY_H(Value, ValueArray)

void printValue(Value value);

bool valuesEqual(Value a, Value b);

ObjString *copyString(struct VM *vm, const char *chars, int length);
ObjString *allocateString(struct VM *vm, char *chars, int length);

void freeObject(Obj *object);
void freeObjects(Obj *objects);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif