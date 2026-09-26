#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "compiler.h"
#include "vm.h"

static void repl(VM *vm) {
    char line[1024];

    while (true) {
        printf("> ");

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        interpretRepl(vm, line);
    }
}

static char* readFile(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file '%s'.", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char *buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read '%s'.", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file '%s'.", path);
        exit(74);
    }
    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

static const char *dumpPathFor(const char *path) {
    static char buffer[1024];
    snprintf(buffer, sizeof(buffer), "%s.slb", path);
    return buffer;
}

// PrototypeHook: writes the compiled prototype out as JSON bytecode.
static bool dumpBytecode(VM *vm, ObjPrototype *prototype, void *context) {
    const char *path = (const char *)context;

    FILE *out = fopen(path, "wb");
    if (out == NULL) {
        fprintf(stderr, "Could not write '%s'.\n", path);
        return false;
    }

    saveBytecode(vm, prototype, out);
    fclose(out);
    printf("Wrote %s\n", path);
    return true;
}

static int exitCodeFor(InterpretResult result) {
    switch (result) {
        case INTERPRET_COMPILE_ERROR: return 65;
        case INTERPRET_RUNTIME_ERROR: return 70;
        case INTERPRET_OK: return 0;
    }
    return 0;
}

// Compile source and run it. With dump set, also writes the JSON bytecode next
// to the source (Python's .pyc behaviour) before execution.
static int runSource(VM *vm, const char *path, bool dump) {
    char *source = readFile(path);
    const char *dumpPath = dump ? dumpPathFor(path) : NULL;

    InterpretResult result = interpret(vm, source,
                                       dumpPath != NULL ? dumpBytecode : NULL,
                                       (void *)dumpPath);
    free(source);

    return exitCodeFor(result);
}

// Load a .slb bytecode file and run it without lexing, parsing, or type
// checking the source.
static int runBytecodeFile(VM *vm, const char *path) {
    char *json = readFile(path);

    vm->canGC = false;
    const char *error = NULL;
    ObjPrototype *prototype = loadBytecode(vm, json, &error);
    if (prototype == NULL) {
        fprintf(stderr, "Could not load bytecode '%s': %s\n", path,
                error != NULL ? error : "unknown error");
        free(json);
        return 65;
    }

    InterpretResult result = runBytecode(vm, prototype, path);
    free(json);

    return exitCodeFor(result);
}

int main(int argc, char *argv[]) {
    VM vm;
    initVM(&vm);

    int exitCode = 0;
    if (argc == 1) {
        repl(&vm);
    } else if (argc == 3 && strcmp(argv[1], "--dump") == 0) {
        exitCode = runSource(&vm, argv[2], true);
    } else if (argc == 2) {
        const char *dot = strrchr(argv[1], '.');
        if (dot != NULL && strcmp(dot, ".slb") == 0) {
            exitCode = runBytecodeFile(&vm, argv[1]);
        } else {
            exitCode = runSource(&vm, argv[1], false);
        }
    } else {
        fprintf(stderr, "Usage: solace <file.slc>         run source file\n");
        fprintf(stderr, "       solace --dump <file.slc>  compile and write <file.slc>.slb\n");
        fprintf(stderr, "       solace <file.slb>         run dumped bytecode\n");
        exitCode = 64;
    }

    freeVM(&vm);
    return exitCode;
}