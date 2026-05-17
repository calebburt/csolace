#ifndef SLC_TYPE_H
#define SLC_TYPE_H

#include "dynamic_array.h"
#include "lexer.h"

struct VM;
struct ObjString;

typedef struct Type {
    struct ObjString *name;
    struct Type *next;
    struct Type *generics;
} Type;

bool typesEqual(Type one, Type two);
// true iff every variant of `sub` appears in `super`. Number is a subtype of
// Number | Nil; Number | Nil is not a subtype of Number.
bool isSubtype(Type sub, Type super);
Type type(struct VM *vm, char *name);
Type tokenType(struct VM *vm, Token token);
Type unionType(struct VM *vm, Type one, Type two);
Type errorType(struct VM *vm);

MAKE_DYNAMIC_ARRAY_H(Type, TypeArray)

// Build a function type. Encoding: name="Function", `generics` points to a chain
// of slot nodes linked by `next`. The first slot holds the return type; each
// subsequent slot holds one parameter type. A slot's `generics` field is the
// actual Type (which keeps its own `next` chain for union variants intact).
// Slots use `name==NULL` as a sentinel so they aren't confused with real types.
Type functionType(struct VM *vm, Type returnType, TypeArray *params);

bool isFunctionType(Type t);

#endif