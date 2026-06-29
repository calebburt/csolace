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

bool typesEqual(Type *one, Type *two);
uint32_t hashType(Type *t);
// true if every variant of `sub` appears in `super`. Number is a subtype of
// Number | Nil; Number | Nil is not a subtype of Number.
bool isSubtype(Type *sub, Type *super);
Type *type(struct VM *vm, char *name);
Type *tokenType(struct VM *vm, Token token);
Type *unionType(struct VM *vm, Type *one, Type *two);
Type *errorType(struct VM *vm);

// Release the heap allocations owned by `t` (variant chain via `next` and the
// slot/held-type chain via `generics`). The top-level Type is passed by reference
// and is itself freed.
void freeType(struct VM *vm, Type *t);

MAKE_DYNAMIC_ARRAY_H(Type*, TypeArray)

// Build a function type. Encoding: name="Function", `generics` points to a chain
// of slot nodes linked by `next`. The first slot holds the return type; each
// subsequent slot holds one parameter type.
Type *functionType(struct VM *vm, Type *returnType, TypeArray *params);

bool isCallableType(Type t);
bool isClassType(Type t);
bool isFunctionType(Type t);

#endif