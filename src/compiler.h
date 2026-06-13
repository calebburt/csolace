#ifndef SLC_COMPILER_H
#define SLC_COMPILER_H

#include "vm.h"

ObjPrototype *compile(VM *vm, const char *source);

// Compile one REPL line against a persistent top-level scope. 
ObjFunction *compileRepl(VM *vm, const char *source, int *baseSlots);

// Discard the persistent REPL scope so the next compileRepl starts fresh.
void resetRepl(void);

void markCompilerRoots(VM *vm);

void markReplRoots(VM *vm);

#endif