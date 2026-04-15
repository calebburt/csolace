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

static void adjustCapacity(Table *table, int capacity) {
    Entry *entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key.type = VAL_NIL;
        entries[i].value.type = VAL_NIL;
    }

    FREE_ARRAY(Entry, table->data, table->capacity);
    table->data = entries;
    table->capacity = capacity;

    for (int i = 0; i < table->count; i++) {
        Entry *entry = &table->data[i];
        if (entry->key.type == VAL_NIL) continue;

        Entry *dest = findEntry(table->data, table->capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }
}

bool tableGet(Table *table, Value key, Value *value) {
    if (table->count == 0) return false;

    Entry *entry = findEntry(table->data, table->capacity, key);
    if (entry->key.type == VAL_NIL) return false;

    *value = entry->value;
    return true;
}

bool tableSet(Table *table, Value key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
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
    entry->value.type = VAL_NIL;
    return true;
}

void tableAddAll(Table *from, Table *to) {
    for (int i = 0; i < from->capacity; i++) {
        Entry *entry = &from->data[i];
        if (entry->key.type == VAL_NIL) continue;

        tableSet(to, entry->key, entry->value);
    }
}
