#ifndef SLC_DEBUG_H
#define SLC_DEBUG_H

#include <stdarg.h>
#include "chunk.h"

void debug(const char *format, ...);

void disassembleChunk(Chunk *chunk, const char *name);
int disassembleInstruction(Chunk *chunk, int offset);

#endif