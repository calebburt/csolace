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
Type type(struct VM *vm, char *name);
Type tokenType(struct VM *vm, Token token);
Type unionType(struct VM *vm, Type one, Type two);
Type errorType(struct VM *vm);

MAKE_DYNAMIC_ARRAY_H(Type, TypeArray)

#endif