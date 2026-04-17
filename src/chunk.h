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
    OP_RETURN,

    OP_PRINT, // temp
} Opcode;

typedef struct {
    int line;
    int num;
} LineInfo;

MAKE_DYNAMIC_ARRAY_H(uint8_t, Code)

MAKE_DYNAMIC_ARRAY_H(LineInfo, LineInfoArray)

typedef struct {
    Code code;
    ValueArray constants;
    LineInfoArray lines;
} Chunk; 

void initChunk(Chunk *chunk);
void freeChunk(Chunk *chunk);
void writeChunk(Chunk *chunk, uint8_t byte, int line);
int addConstant(Chunk *chunk, Value value);

LineInfo getLine(Chunk *chunk, int offset);

#endif