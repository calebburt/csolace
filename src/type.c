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

// `Any` is the universal supertype: a set containing Any accepts every variant.
static bool isAny(Type t) {
    if (t.name == NULL || t.name->length != 3) return false;
    return memcmp(t.name->chars, "Any", 3) == 0;
}

static bool containsVariant(Type *set, Type *variant) {
    for (Type *cur = set; cur != NULL; cur = cur->next) {
        if (isAny(*cur)) return true;
        if (sameVariant(*cur, *variant)) return true;
    }
    return false;
}

// Set equality over the variant chain: each variant of `one` must appear in
// `two` and vice versa. Ignores ordering and duplicates.
bool typesEqual(Type *one, Type *two) {
    return isSubtype(one, two) && isSubtype(two, one);
}

uint32_t hashType(Type *t) {
    uint32_t hash = 0;
    for (Type *cur = t; cur != NULL; cur = cur->next) {
        if (cur->name == NULL) {
            hash ^= 0x9e3779b9; // Arbitrary value for unnamed variants.
        } else {
            hash ^= hashValue(OBJ_VAL(cur->name)); // Hash based on the name string.
        }
    }
    return hash;
}

bool isSubtype(Type *sub, Type *super) {
    for (Type *cur = sub; cur != NULL; cur = cur->next) {
        if (!containsVariant(super, cur)) return false;
    }
    return true;
}

Type *type(VM *vm, char *name) {
    Type *type = ALLOCATE(vm, Type, 1);
    type->name = copyString(vm, name, strlen(name));
    type->next = NULL;
    type->generics = NULL;
    return type;
}

Type *tokenType(VM *vm, Token token) {
    Type *type = ALLOCATE(vm, Type, 1);
    type->name = copyString(vm, token.start, token.length);
    type->next = NULL;
    type->generics = NULL;
    return type;
}

// Build a fresh chain of every variant in `one` followed by every variant in
// `two`. Each link is heap-allocated so the chain outlives the call (the old
// version stored &two, a pointer into this function's stack frame). Duplicate
// variants are skipped so set semantics in typesEqual stay stable.
Type *unionType(VM *vm, Type *one, Type *two) {
    Type *result = one;
    result->next = NULL;
    Type *tail = result;

    Type *src = one->next;
    while (src != NULL) {
        if (!containsVariant(result, src)) {
            Type *node = ALLOCATE(vm, Type, 1);
            *node = *src;
            node->next = NULL;
            tail->next = node;
            tail = node;
        }
        src = src->next;
    }

    for (src = two; src != NULL; src = src->next) {
        if (!containsVariant(result, src)) {
            Type *node = ALLOCATE(vm, Type, 1);
            *node = *src;
            node->next = NULL;
            tail->next = node;
            tail = node;
        }
    }

    return result;
}

Type *errorType(VM *vm) {
    return type(vm, "_ErrorType");
}

static Type *makeSlot(VM *vm, Type *held) {
    Type *slot = ALLOCATE(vm, Type, 1);
    slot->name = NULL;
    slot->next = NULL;
    Type *copy = ALLOCATE(vm, Type, 1);
    copy = held;
    slot->generics = copy;
    return slot;
}

Type *functionType(VM *vm, Type *returnType, TypeArray *params) {
    Type *result = ALLOCATE(vm, Type, 1);
    result->name = copyString(vm, "Function", 8);
    result->next = NULL;

    Type *retSlot = makeSlot(vm, returnType);
    result->generics = retSlot;

    Type *tail = retSlot;
    for (int i = 0; i < params->count; i++) {
        Type *slot = makeSlot(vm, params->data[i]);
        tail->next = slot;
        tail = slot;
    }
    return result;
}

void freeType(VM *vm, Type *t) {
    Type *slot = t->generics;
    while (slot != NULL) {
        Type *nextSlot = slot->next;
        if (slot->generics != NULL) {
            freeType(vm, slot->generics);
        }
        FREE(vm, Type, slot);
        slot = nextSlot;
    }

    Type *variant = t->next;
    while (variant != NULL) {
        Type *nextVariant = variant->next;
        FREE(vm, Type, variant);
        variant = nextVariant;
    }
    FREE(vm, Type, t);
}

bool isFunctionType(Type t) {
    if (t.name == NULL) return false;
    if (t.name->length != 8) return false;
    return memcmp(t.name->chars, "Function", 8) == 0;
}
bool isClassType(Type t) {
    if (t.name == NULL) return false;
    if (t.name->length != 5) return false;
    return memcmp(t.name->chars, "Class", 5) == 0;
}
bool isCallableType(Type t) {
    return isFunctionType(t) || isClassType(t);
}

MAKE_DYNAMIC_ARRAY(Type*, TypeArray)
