#ifndef SLC_TABLE_H
#define SLC_TABLE_H

#include "common.h"
#include "value.h"
#include "dynamic_array.h"

#define TABLE_MAX_LOAD 0.75

#define MAKE_TABLE_H(table_name, entry_name, key_type, value_type, hash_fn, key_eq_fn) \
    typedef struct { \
        key_type key; \
        value_type value; \
        bool occupied; \
        bool tombstone; \
    } entry_name; \
    \
    typedef struct { \
        entry_name *data; \
        int count; \
        int capacity; \
    } table_name; \
    \
    void init##table_name(table_name *table); \
    void free##table_name(VM *vm, table_name *table); \
    bool get##table_name(table_name *table, key_type key, value_type *value); \
    bool set##table_name(VM *vm, table_name *table, key_type key, value_type value); \
    bool delete##table_name(table_name *table, key_type key); \
    void addAll##table_name(VM *vm, table_name *from, table_name *to);

#define MAKE_TABLE(table_name, entry_name, key_type, value_type, hash_fn, key_eq_fn) \
    \
    static entry_name *findEntry_##table_name( \
            entry_name *entries, int capacity, key_type key) { \
        uint32_t index = hash_fn(key) % capacity; \
        entry_name *tombstone = NULL; \
        while (true) { \
            entry_name *entry = &entries[index]; \
            if (!entry->occupied) { \
                if (!entry->tombstone) { \
                    return tombstone != NULL ? tombstone : entry; \
                } else if (tombstone == NULL) { \
                    tombstone = entry; \
                } \
            } else if (key_eq_fn(entry->key, key)) { \
                return entry; \
            } \
            index = (index + 1) % capacity; \
        } \
    } \
    \
    static void adjustCapacity_##table_name(VM *vm, table_name *table, int capacity) { \
        entry_name *entries = ALLOCATE(vm, entry_name, capacity); \
        for (int i = 0; i < capacity; i++) { \
            entries[i].occupied = false; \
            entries[i].tombstone = false; \
        } \
        \
        entry_name *oldEntries = table->data; \
        int oldCapacity = table->capacity; \
        table->data = entries; \
        table->capacity = capacity; \
        table->count = 0; \
        \
        for (int i = 0; i < oldCapacity; i++) { \
            entry_name *entry = &oldEntries[i]; \
            if (!entry->occupied) continue; \
            entry_name *dest = findEntry_##table_name(entries, capacity, entry->key); \
            dest->key = entry->key; \
            dest->value = entry->value; \
            dest->occupied = true; \
            dest->tombstone = false; \
            table->count++; \
        } \
        \
        FREE_ARRAY(vm, entry_name, oldEntries, oldCapacity); \
    } \
    \
    void init##table_name(table_name *table) { \
        table->data = NULL; \
        table->count = 0; \
        table->capacity = 0; \
    } \
    \
    void free##table_name(VM *vm, table_name *table) { \
        FREE_ARRAY(vm, entry_name, table->data, table->capacity); \
        init##table_name(table); \
    } \
    \
    bool get##table_name(table_name *table, key_type key, value_type *value) { \
        if (table->count == 0) return false; \
        entry_name *entry = findEntry_##table_name(table->data, table->capacity, key); \
        if (!entry->occupied) return false; \
        *value = entry->value; \
        return true; \
    } \
    \
    bool set##table_name(VM *vm, table_name *table, key_type key, value_type value) { \
        if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) { \
            int capacity = GROW_CAPACITY(table->capacity); \
            adjustCapacity_##table_name(vm, table, capacity); \
        } \
        entry_name *entry = findEntry_##table_name(table->data, table->capacity, key); \
        bool isNewKey = !entry->occupied; \
        if (isNewKey && !entry->tombstone) table->count++; \
        entry->key = key; \
        entry->value = value; \
        entry->occupied = true; \
        entry->tombstone = false; \
        return isNewKey; \
    } \
    \
    bool delete##table_name(table_name *table, key_type key) { \
        if (table->count == 0) return false; \
        entry_name *entry = findEntry_##table_name(table->data, table->capacity, key); \
        if (!entry->occupied) return false; \
        entry->occupied = false; \
        entry->tombstone = true; \
        return true; \
    } \
    \
    void addAll##table_name(VM *vm, table_name *from, table_name *to) { \
        for (int i = 0; i < from->capacity; i++) { \
            entry_name *entry = &from->data[i]; \
            if (!entry->occupied) continue; \
            set##table_name(vm, to, entry->key, entry->value); \
        } \
    }

MAKE_TABLE_H(Table, Entry, Value, Value, hashValue, valuesEqual)

#endif
