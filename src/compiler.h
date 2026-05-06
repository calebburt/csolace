#ifndef SLC_COMPILER_H
#define SLC_COMPILER_H

#include "vm.h"

ObjPrototype *compile(VM *vm, const char *source, Chunk *chunk);

#endif