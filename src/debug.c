#include "common.h"
#include "debug.h"
#include "value.h"

void debug(const char* format, ...) {
    #ifdef SLC_DEBUG
    va_list args;

    va_start(args, format);

    printf(" ");
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
    printValue(chunk->constants.data[constant]);
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
        case OP_NIL: return simpleInstruction("OP_NIL", offset);
        case OP_TRUE: return simpleInstruction("OP_TRUE", offset);
        case OP_FALSE: return simpleInstruction("OP_FALSE", offset);
        case OP_EQUAL: return simpleInstruction("OP_EQUAL", offset);
        case OP_GREATER: return simpleInstruction("OP_GREATER", offset);
        case OP_LESS: return simpleInstruction("OP_LESS", offset);
        case OP_GREATER_EQUAL: return simpleInstruction("OP_GREATER_EQUAL", offset);
        case OP_LESS_EQUAL: return simpleInstruction("OP_LESS_EQUAL", offset);
        case OP_ADD: return simpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT: return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY: return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE: return simpleInstruction("OP_DIVIDE", offset);
        case OP_NOT: return simpleInstruction("OP_NOT", offset);
        case OP_NEGATE:
            return simpleInstruction("OP_NEGATE", offset);
        default:
            printf("Unknown Opcode %d\n", instruction);
            return offset + 1;
    }
}
