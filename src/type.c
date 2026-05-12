#include "type.h"
#include "memory.h"
#include "value.h"
#include "vm.h"
#include <string.h>

static bool sameVariant(Type a, Type b) {
    if (a.name == NULL || b.name == NULL) return a.name == b.name;
    if (a.name->length != b.name->length) return false;
    return memcmp(a.name->chars, b.name->chars, a.name->length) == 0;
}

static bool containsVariant(Type set, Type variant) {
    for (Type *cur = &set; cur != NULL; cur = cur->next) {
        if (sameVariant(*cur, variant)) return true;
    }
    return false;
}

// Set equality over the variant chain: each variant of `one` must appear in
// `two` and vice versa. Ignores ordering and duplicates.
bool typesEqual(Type one, Type two) {
    return isSubtype(one, two) && isSubtype(two, one);
}

bool isSubtype(Type sub, Type super) {
    for (Type *cur = &sub; cur != NULL; cur = cur->next) {
        if (!containsVariant(super, *cur)) return false;
    }
    return true;
}

Type type(VM *vm, char *name) {
    Type type;
    type.name = copyString(vm, name, strlen(name));
    type.next = NULL;
    type.generics = NULL;
    return type;
}

Type tokenType(VM *vm, Token token) {
    Type type;
    type.name = copyString(vm, token.start, token.length);
    type.next = NULL;
    type.generics = NULL;
    return type;
}

// Build a fresh chain of every variant in `one` followed by every variant in
// `two`. Each link is heap-allocated so the chain outlives the call (the old
// version stored &two, a pointer into this function's stack frame). Duplicate
// variants are skipped so set semantics in typesEqual stay stable.
Type unionType(VM *vm, Type one, Type two) {
    Type result = one;
    result.next = NULL;
    Type *tail = &result;

    Type *src = one.next;
    while (src != NULL) {
        if (!containsVariant(result, *src)) {
            Type *node = ALLOCATE(Type, 1);
            *node = *src;
            node->next = NULL;
            tail->next = node;
            tail = node;
        }
        src = src->next;
    }

    for (src = &two; src != NULL; src = src->next) {
        if (!containsVariant(result, *src)) {
            Type *node = ALLOCATE(Type, 1);
            *node = *src;
            node->next = NULL;
            tail->next = node;
            tail = node;
        }
    }

    return result;
}

Type errorType(VM *vm) {
    return type(vm, "_ErrorType");
}

MAKE_DYNAMIC_ARRAY(Type, TypeArray)
