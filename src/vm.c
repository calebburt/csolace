#include "common.h"
#include "chunk.h"
#include "value.h"
#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include <stdarg.h>

static void resetStack(VM *vm) {
    vm->stackTop = vm->stack;
}

char* getLineOfString(const char* str, int lineNo) {
    if (lineNo < 1) return NULL;

    const char* start = str;
    int currentLine = 1;
    
    // Skip to the start of the requested line
    while (currentLine < lineNo && start != NULL) {
        start = strchr(start, '\n');
        if (start) start++; // Move past the newline
        currentLine++;
    }
    
    if (start == NULL || *start == '\0') return NULL;
    
    // Find the end of the line
    const char* end = strchr(start, '\n');
    size_t len = end ? (size_t)(end - start) : strlen(start);
    
    // Allocate memory and copy the line
    char* result = (char*)malloc(len + 1);
    if (result) {
        strncpy(result, start, len);
        result[len] = '\0';
    }
    return result;
}

static void runtimeError(VM *vm, const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "\033[31mRuntimeError: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\033[0m\n");
    va_end(args);

    size_t instruction = vm->ip - vm->chunk->code.data - 1;
    int line = getLine(vm->chunk, (int)instruction).line;
    fprintf(stderr, "\033[33m at line %d\033[0m\n", line);
    fprintf(stderr, "%3.0d | %s \n", line, getLineOfString(vm->source, line));
    resetStack(vm);
}

void initVM(VM *vm) {
    vm->chunk = NULL;
    vm->objects = NULL;
    initTable(&vm->globals);
    resetStack(vm);
}

void freeVM(VM *vm) {
    freeTable(&vm->globals);
    freeObjects(vm->objects);
}

void push(VM *vm, Value value) {
    *vm->stackTop = value;
    vm->stackTop++;
}

Value pop(VM *vm) {
    vm->stackTop--;
    return *vm->stackTop;
}

static Value peek(VM *vm, int distance) {
    return vm->stackTop[-1 - distance];
}

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static InterpretResult run(VM *vm) {
#define READ_BYTE() (*vm->ip++)
#define READ_CONSTANT() (vm->chunk->constants.data[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define READ_SHORT() ((vm->ip += 2, (uint16_t)((vm->ip[-2] << 8) | vm->ip[-1])))

#define BINARY_OP(vm, valueType, op) \
    do { \
      if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) { \
        runtimeError(vm, "Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      double b = AS_NUMBER(pop(vm)); \
      double a = AS_NUMBER(pop(vm)); \
      push(vm, valueType(a op b)); \
    } while (false)

    while (true) {
#ifdef SLC_DEBUG
        printf(" [ ");
        for (Value *slot = vm->stack; slot < vm->stackTop; slot++) {
            printf("[");
            printValue(*slot);
            printf("]");
        }
        printf(" ]\n ");

        disassembleInstruction(vm->chunk, (int)(vm->ip - vm->chunk->code.data));
#endif
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(vm, constant);
                break;
            }
            case OP_NIL: push(vm, NIL_VAL); break;
            case OP_TRUE: push(vm, BOOL_VAL(true)); break;
            case OP_FALSE: push(vm, BOOL_VAL(false)); break;
            case OP_POP: pop(vm); break;
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm->globals, OBJ_VAL(name), &value)) {
                    runtimeError(vm, "Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, value);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString *name = READ_STRING();
                tableSet(&vm->globals, OBJ_VAL(name), peek(vm, 0));
                break;
            }
            case OP_EQUAL: {
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER: BINARY_OP(vm, BOOL_VAL, >); break;
            case OP_LESS: BINARY_OP(vm, BOOL_VAL, <); break;
            case OP_GREATER_EQUAL: BINARY_OP(vm, BOOL_VAL, >=); break;
            case OP_LESS_EQUAL: BINARY_OP(vm, BOOL_VAL, <=); break;
            case OP_ADD: {
                if (IS_STRING(peek(vm, 0)) && IS_STRING(peek(vm, 1))) {
                    ObjString *b = AS_STRING(pop(vm));
                    ObjString *a = AS_STRING(pop(vm));
                    int length = a->length + b->length;
                    char *chars = ALLOCATE(char, length + 1);
                    memcpy(chars, a->chars, a->length);
                    memcpy(chars + a->length, b->chars, b->length);
                    chars[length] = '\0';
                    push(vm, OBJ_VAL(allocateString(vm, chars, length)));
                } else if (IS_NUMBER(peek(vm, 0)) && IS_NUMBER(peek(vm, 1))) {
                    double b = AS_NUMBER(pop(vm));
                    double a = AS_NUMBER(pop(vm));
                    push(vm, NUMBER_VAL(a + b));
                } else {
                    runtimeError(vm, "Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT: BINARY_OP(vm, NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(vm, NUMBER_VAL, *); break;
            case OP_DIVIDE: BINARY_OP(vm, NUMBER_VAL, /); break;
            case OP_NOT: push(vm, BOOL_VAL(isFalsey(pop(vm)))); break;
            case OP_NEGATE: {
                if (!IS_NUMBER(peek(vm, 0))) {
                    runtimeError(vm, "Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, NUMBER_VAL(-AS_NUMBER(pop(vm))));
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                vm->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(vm, 0))) vm->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                vm->ip -= offset;
                break;
            }
            case OP_RETURN: {
                return INTERPRET_OK;
            }

            case OP_PRINT: { //temp
                printValue(pop(vm));
                printf("\n");
                break;
            }
        }
    }

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_STRING
#undef READ_SHORT

#undef BINARY_OP
}

InterpretResult interpret(VM *vm, const char *source) {
    Chunk chunk;
    initChunk(&chunk);

    if (!compile(vm, source, &chunk)) {
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    vm->chunk = &chunk;
    vm->ip = vm->chunk->code.data;
    vm->source = (char*)source;

    InterpretResult result = run(vm);
    
    freeChunk(&chunk);
    return result;
}
