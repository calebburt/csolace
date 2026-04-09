#ifndef SLC_CHUNK_H
#define SLC_CHUNK_H

#include "common.h"
#include "dynamic_array.h"
#include "object.h"

typedef enum {
    OP_CONSTANT,
    OP_RETURN,
} Opcode;

typedef struct {
    int line;
    int num;
} LineInfo;

MAKE_DYNAMIC_ARRAY_H(uint8_t, Code)

MAKE_DYNAMIC_ARRAY_H(LineInfo, LineInfoArray)

typedef struct {
    Code code;
    ObjectArray constants;
    LineInfoArray lines;
} Chunk; 

void initChunk(Chunk *chunk);
void freeChunk(Chunk *chunk);
void writeChunk(Chunk *chunk, uint8_t byte, int line);
int addConstant(Chunk *chunk, Object object);

LineInfo getLine(Chunk *chunk, int offset);

#endif