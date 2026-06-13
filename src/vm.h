#ifndef SLC_VM_H
#define SLC_VM_H

#include "common.h"
#include "chunk.h"
#include "value.h"
#include "table.h"
#include "type.h"

// The active compiler lives on the Parser (defined in compiler.c); the VM holds
// a pointer to it during compilation so the GC can reach compiler roots.
typedef struct Parser Parser;

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)
#define NATIVES_MAX UINT8_COUNT

typedef struct {
    ObjFunction *function;
    uint8_t *ip;
    Value *slots;
} CallFrame;

typedef struct VM {
    CallFrame frames[FRAMES_MAX]; // will replace with dynamic array maybe
    int frameCount;

    Chunk *chunk;
    uint8_t *ip;

    Value stack[STACK_MAX]; // will replace with dynamic array maybe
    Value *stackTop;

    ObjUpvalue *openUpvalues;
    Obj *objects;

    // GC worklist of marked-but-not-yet-blackened objects (tricolor marking).
    int grayCount;
    int grayCapacity;
    Obj **grayStack;

    // Heap size (in bytes) at which the next collection is triggered; grows by
    // GC_HEAP_GROW_FACTOR after each collection.
    size_t nextGC;
    // Collection is only safe once execution begins. The type checker allocates
    // ObjStrings that live solely as Type values on the C stack (unreachable by
    // the collector), so GC must stay off during init and compilation. interpret()
    // flips this true right before run(), when every live object is rooted.
    bool canGC;

    // Non-NULL only while compile() is running; lets markCompilerRoots() walk
    // the in-flight compiler chain.
    Parser *parser;

    // Natives have their own namespace addressed by OP_GET_NATIVE <idx>.
    // Registered before compilation; the compiler resolves bare identifiers
    // against this table when local resolution misses.
    ObjNative *natives[NATIVES_MAX];
    Type nativeTypes[NATIVES_MAX];
    int nativeCount;

    char *source;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM(VM *vm);
void freeVM(VM *vm);

InterpretResult interpret(VM *vm, const char *source);
InterpretResult interpretRepl(VM *vm, const char *source);

void push(VM *vm, Value value);
Value pop(VM *vm);

// Register a native callable. `params` may be NULL for zero-arg natives.
// `name` must outlive the VM (string literals are fine).
void defineNative(VM *vm, const char *name, NativeFn fn,
                  Type returnType, TypeArray *params);

#endif