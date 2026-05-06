#include "type.h"
#include "value.h"
#include "vm.h"
#include <string.h>

bool typesEqual(Type one, Type two) {
    if (one.name == NULL || two.name == NULL) return one.name == two.name;
    if (one.name->length != two.name->length) return false;
    return memcmp(one.name->chars, two.name->chars, one.name->length) == 0;
}

Type type(VM *vm, char *name) {
    Type type;
    type.name = copyString(vm, name, strlen(name));
    return type;
}

Type tokenType(VM *vm, Token token) {
    Type type;
    type.name = copyString(vm, token.start, token.length);
    return type;
}

Type unionType(VM *vm, Type one, Type two) {
    one.next = &two;
    return one;
}

Type errorType(VM *vm) {
    return type(vm, "_ErrorType");
}

MAKE_DYNAMIC_ARRAY(Type, TypeArray)
