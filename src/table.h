#ifndef SLC_TABLE_H
#define SLC_TABLE_H

#include "common.h"
#include "value.h"
#include "dynamic_array.h"

typedef struct {
    Value key;
    Value value;
} Entry;

MAKE_DYNAMIC_ARRAY_H(Entry, Table)

#define TABLE_MAX_LOAD 0.75

bool tableGet(Table *table, Value key, Value *value);
bool tableSet(Table *table, Value key, Value value);
bool tableDelete(Table *table, Value key);
void tableAddAll(Table *from, Table *to);

#endif