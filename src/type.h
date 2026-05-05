#ifndef SLC_TYPE_H
#define SLC_TYPE_H

#include "value.h"
#include "dynamic_array.h"
#include "lexer.h"
#include "vm.h"

typedef struct Type {
    ObjString *name;
    struct Type *next;
    struct Type *generics;
} Type;

bool typesEqual(Type one, Type two);
Type type(VM *vm, char *name);
Type tokenType(VM *vm, Token token);
Type unionType(VM *vm, Type one, Type two);
Type errorType(VM *vm);

#endif