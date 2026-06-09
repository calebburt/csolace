#include "table.h"

MAKE_DYNAMIC_ARRAY(Entry, Table)

static Entry *findEntry(Entry* entries, int capacity, Value key) {
    uint32_t index = hashValue(key) % capacity;
    Entry *tombstone = NULL;
    while (true) {
        Entry *entry = &entries[index];
        if (IS_NIL(entry->key)) {
            if (IS_NIL(entry->value)) {
                return tombstone != NULL ? tombstone : entry;
            } else {
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (valuesEqual(entry->key, key)) {
            return entry;
        }

        index = (index + 1) % capacity;
    }
}

static void adjustCapacity(VM *vm, Table *table, int capacity) {
    Entry *entries = ALLOCATE(vm, Entry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key.type = VAL_NIL;
        entries[i].value.type = VAL_NIL;
    }

    Entry *oldEntries = table->data;
    int oldCapacity = table->capacity;

    table->data = entries;
    table->capacity = capacity;
    table->count = 0;

    for (int i = 0; i < oldCapacity; i++) {
        Entry *entry = &oldEntries[i];
        if (entry->key.type == VAL_NIL) continue;

        Entry *dest = findEntry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(vm, Entry, oldEntries, oldCapacity);
}

bool tableGet(Table *table, Value key, Value *value) {
    if (table->count == 0) return false;

    Entry *entry = findEntry(table->data, table->capacity, key);
    if (entry->key.type == VAL_NIL) return false;

    *value = entry->value;
    return true;
}

bool tableSet(VM *vm, Table *table, Value key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(vm, table, capacity);
    }

    Entry *entry = findEntry(table->data, table->capacity, key);
    bool isNewKey = entry->key.type == VAL_NIL;
    if (isNewKey && IS_NIL(entry->value)) table->count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool tableDelete(Table *table, Value key) {
    if (table->count == 0) return false;

    Entry *entry = findEntry(table->data, table->capacity, key);
    if (entry->key.type == VAL_NIL) return false;

    entry->key.type = VAL_NIL;
    entry->value = BOOL_VAL(true); // tombstone
    return true;
}

void tableAddAll(VM *vm, Table *from, Table *to) {
    for (int i = 0; i < from->capacity; i++) {
        Entry *entry = &from->data[i];
        if (entry->key.type == VAL_NIL) continue;

        tableSet(vm, to, entry->key, entry->value);
    }
}
