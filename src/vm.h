#ifndef SLC_VM_H
#define SLC_VM_H

#include "common.h"
#include "chunk.h"
#include "value.h"

#define STACK_MAX 256

typedef struct VM {
    Chunk *chunk;
    uint8_t *ip;
    Value stack[STACK_MAX]; // will replace with dynamic array maybe
    Value *stackTop;
    Obj *objects;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM(VM *vm);
void freeVM(VM *vm);

InterpretResult interpret(VM *vm, const char *source);

void push(VM *vm, Value value);
Value pop(VM *vm);

#endif