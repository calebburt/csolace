#ifndef SLC_DEBUG_H
#define SLC_DEBUG_H

#include <stdarg.h>
#include "chunk.h"

#ifdef SLC_DEBUG
void debug(const char* format, ...);
#else
#define debug(...) ((void)0)
#endif

void disassembleChunk(Chunk *chunk, const char *name);
int disassembleInstruction(Chunk *chunk, int offset);

#endif