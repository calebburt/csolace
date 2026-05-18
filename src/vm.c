#include "common.h"
#include "chunk.h"
#include "value.h"
#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include <stdarg.h>
#include <time.h>

static void resetStack(VM *vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
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
    
    for (int i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame *frame = &vm->frames[i];
        ObjPrototype *function = frame->function;
        size_t instruction = frame->ip - function->chunk.code.data - 1;
        int line = getLine(&function->chunk, (int)instruction).line;
        const char *name = function->name != NULL ? function->name->chars : "<script>";
        fprintf(stderr, "\033[33m at line %d in %s\033[0m\n", line, name);
        char *src = getLineOfString(vm->source, line);
        fprintf(stderr, "%3.0d | %s \n", line, src != NULL ? src : "");
        free(src);
    }

    resetStack(vm);
}

static bool clockNative(VM *vm, int argCount, Value *args, Value *out) {
    (void)vm; (void)argCount; (void)args;
    *out = NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
    return true;
}

static bool printNative(VM *vm, int argCount, Value *args, Value *out) {
    printValue(args[0]);
    *out = NIL_VAL;
    return true;
}

static void defineBuiltinNatives(VM *vm) {
    defineNative(vm, "clock", clockNative, type(vm, "Number"), NULL);

    TypeArray printParams;
    initTypeArray(&printParams);
    appendTypeArray(&printParams, type(vm, "Any"));
    defineNative(vm, "print", printNative, type(vm, "Nil"), &printParams);
    freeTypeArray(&printParams);
}

void initVM(VM *vm) {
    vm->chunk = NULL;
    vm->objects = NULL;
    vm->nativeCount = 0;
    resetStack(vm);
    defineBuiltinNatives(vm);
}

void defineNative(VM *vm, const char *name, NativeFn fn,
                  Type returnType, TypeArray *params) {
    if (vm->nativeCount == NATIVES_MAX) {
        fprintf(stderr, "Too many native functions registered.\n");
        exit(70);
    }
    TypeArray empty;
    if (params == NULL) {
        initTypeArray(&empty);
        params = &empty;
    }
    int idx = vm->nativeCount++;
    vm->natives[idx] = newNative(vm, fn, name);
    vm->nativeTypes[idx] = functionType(vm, returnType, params);
    if (params == &empty) freeTypeArray(&empty);
}

void freeVM(VM *vm) {
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

static bool call(VM *vm, ObjPrototype *function, int argCount) {
    if (argCount != function->paramaters.count) {
        runtimeError(vm, "Expected %d arguments but got %d.", function->paramaters.count, argCount);
        return false;
    }

    if (vm->frameCount == FRAMES_MAX) {
        runtimeError(vm, "Stack overflow.");
        return false;
    }

    CallFrame *frame = &vm->frames[vm->frameCount++];
    frame->function = function;
    frame->ip = function->chunk.code.data;
    frame->slots = vm->stackTop - argCount - 1; // -1 for the callee

    return true;
}

static bool callValue(VM *vm, Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_PROTOTYPE: {
                return call(vm, AS_PROTOTYPE(callee), argCount);
            }
            case OBJ_NATIVE: {
                ObjNative *native = AS_NATIVE(callee);
                Value result;
                bool succeeded = native->function(vm, argCount, vm->stackTop - argCount, &result);
                vm->stackTop -= argCount + 1; // Pop arguments and the callee
                push(vm, result);
                return succeeded;
            }
            default:
                break; // Non-callable object type
        }
    }
    runtimeError(vm, "Can only call functions and classes.");
    return false;
}

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static InterpretResult run(VM *vm) {
    CallFrame *frame = &vm->frames[vm->frameCount - 1];
#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() (frame->function->chunk.constants.data[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define READ_SHORT() ((frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1])))

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

        disassembleInstruction(&frame->function->chunk, (int)(frame->ip - frame->function->chunk.code.data));
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
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(vm, frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(vm, 0);
                break;
            }
            case OP_GET_NATIVE: {
                uint8_t idx = READ_BYTE();
                push(vm, OBJ_VAL(vm->natives[idx]));
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
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(vm, 0))) frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }
            case OP_CALL: {
                int argCount = READ_BYTE();
                if (!callValue(vm, peek(vm, argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm->frames[vm->frameCount - 1];
                break;
            }
            case OP_RETURN: {
                Value result = pop(vm);
                vm->frameCount--;
                if (vm->frameCount == 0) {
                    pop(vm); // pop the script function
                    return INTERPRET_OK;
                }

                vm->stackTop = frame->slots;
                push(vm, result);
                frame = &vm->frames[vm->frameCount - 1];
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
    resetStack(vm);

    ObjPrototype *function = compile(vm, source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    vm->source = (char*)source;

    push(vm, OBJ_VAL(function));
    call(vm, function, 0);

    return run(vm);
}
