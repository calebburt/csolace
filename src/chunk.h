#ifndef SLC_CHUNK_H
#define SLC_CHUNK_H

#include "common.h"
#include "dynamic_array.h"
#include "value.h"

typedef enum {
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_NATIVE,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_CLOSE_UPVALUE,
    OP_GET_FIELD,
    OP_GET_METHOD,
    OP_SET_FIELD,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_GREATER_EQUAL,
    OP_LESS_EQUAL,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_NEGATE,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_CALL,
    OP_CLOSURE,
    OP_RETURN,
    OP_CLASS,
    OP_HALT,
    OP_METHOD
} Opcode;

typedef struct {
    int line;
    int num;
} LineInfo;

MAKE_DYNAMIC_ARRAY_H(uint8_t, Code)

MAKE_DYNAMIC_ARRAY_H(LineInfo, LineInfoArray)

typedef struct Chunk {
    Code code;
    ValueArray constants;
    LineInfoArray lines;
} Chunk;

// ObjPrototype lives here (rather than in value.h) so its `chunk` field can be
// embedded by value — value.h only sees a forward declaration.
struct ObjPrototype {
    Obj obj;
    Chunk chunk;
    ObjString *name;
    TypeArray paramaters;
    Type *returnType;
    int upvalueCount;
};

void initChunk(Chunk *chunk);
void freeChunk(VM *vm, Chunk *chunk);
void writeChunk(VM *vm, Chunk *chunk, uint8_t byte, int line);
int addConstant(VM *vm, Chunk *chunk, Value value);

LineInfo getLine(Chunk *chunk, int offset);

#endif