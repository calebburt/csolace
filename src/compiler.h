#ifndef SLC_COMPILER_H
#define SLC_COMPILER_H

#include "vm.h"

bool compile(VM *vm, const char *source, Chunk *chunk);

#endif