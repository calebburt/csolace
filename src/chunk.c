#include "chunk.h"

MAKE_DYNAMIC_ARRAY(uint8_t, Code)

MAKE_DYNAMIC_ARRAY(LineInfo, LineInfoArray)

void initChunk(Chunk *chunk) {
    initCode(&chunk->code);
    initLineInfoArray(&chunk->lines);
    initValueArray(&chunk->constants);
}

void freeChunk(VM *vm, Chunk *chunk) {
    freeCode(vm, &chunk->code);
    freeLineInfoArray(vm, &chunk->lines);
    freeValueArray(vm, &chunk->constants);
}

void writeChunk(VM *vm, Chunk *chunk, uint8_t byte, int line) {
    appendCode(vm, &chunk->code, byte);
    if (chunk->lines.data == NULL || !(chunk->lines.data[chunk->lines.count-1].line == line)) {
        appendLineInfoArray(vm, &chunk->lines, (LineInfo){line, 1});
    } else {
        chunk->lines.data[chunk->lines.count-1].num++;
    }
}

int addConstant(VM *vm, Chunk *chunk, Value value) {
    appendValueArray(vm, &chunk->constants, value);
    return chunk->constants.count - 1;
}

LineInfo getLine(Chunk *chunk, int offset) {
    int cumulative = 0;
    for (int j = 0; j < chunk->lines.count; j++) {
        cumulative += chunk->lines.data[j].num;
        if (offset < cumulative) {
            return chunk->lines.data[j];
        }
    }
    return (LineInfo){-1, -1};
}
