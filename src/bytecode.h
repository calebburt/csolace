#ifndef SLC_BYTECODE_H
#define SLC_BYTECODE_H

#include <stdio.h>

#include "chunk.h"

// Format version. Bumped when the opcode set, value encoding, or prototype 
// layout changes so stale/foreign files fail loudly instead of mis-running.
#define SLC_BYTECODE_VERSION 1

// Serialize a compiled prototype tree as JSON, mirroring the in-memory
// ObjPrototype/Chunk/Value/Type layout field-for-field. Returns false if a
// constant holds an object type the format can't represent (the compiler only
// ever emits strings and nested prototypes, so this shouldn't happen).
bool saveBytecode(VM *vm, ObjPrototype *prototype, FILE *out);

// Parse a JSON bytecode document into a fresh prototype tree. On failure
// returns NULL and points *outError at a static (non-owning) message.
ObjPrototype *loadBytecode(VM *vm, const char *json, const char **outError);

#endif