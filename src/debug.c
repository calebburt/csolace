#include "common.h"
#include "debug.h"

void debug(const char* format, ...) {
    #ifdef SLC_DEBUG
    va_list args;

    va_start(args, format);

    vprintf(format, args);

    va_end(args);
    #endif
}

void disassembleChunk(Chunk *chunk, const char *name) {
    printf("-- %s --\n", name);
    for (int offset=0;offset<chunk->code.count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

static int simpleInstruction(const char *name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

static int constantInstruction(const char *name, Chunk *chunk, int offset) {
    uint8_t constant = chunk->code.data[offset+1];
    printf("%-16s %4d '", name, constant);
    printObject(chunk->constants.data[constant]);
    printf("'\n");
    return offset + 2;
}

int disassembleInstruction(Chunk *chunk, int offset) {
    printf("%04d ", offset);

    LineInfo line = getLine(chunk, offset);
    if (offset > 0 && line.line == getLine(chunk, offset + 1).line) {
        printf("   | ");
    } else {
        printf("%4d ", line.line);
    }

    Opcode instruction = chunk->code.data[offset];
    switch (instruction) {
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        default:
            printf("Unknown Opcode %d\n", instruction);
            return offset + 1;
    }
}
