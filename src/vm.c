#include "chunk.h"
#include "value.h"
#include "vm.h"
#include "compiler.h"
#include <stdarg.h>
#include <time.h>
#include <math.h>

#ifdef SLC_DEBUG
#include "debug.h"
#endif

static void resetStack(VM *vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
    vm->openUpvalues = NULL;
}

char* getLineOfString(VM *vm, const char* str, int lineNo) {
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
    char* result = (char*)ALLOCATE(vm, char, len + 1);
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
        ObjPrototype *function = frame->function->prototype;
        size_t instruction = frame->ip - function->chunk.code.data - 1;
        int line = getLine(&function->chunk, (int)instruction).line;
        const char *name = function->name != NULL ? function->name->chars : "<script>";
        fprintf(stderr, "\033[33m at line %d in %s\033[0m\n", line, name);
        char *src = getLineOfString(vm, vm->source, line);
        fprintf(stderr, "%3.0d | %s \n", line, src != NULL ? src : "");
        FREE(vm, char*, src);
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
    printf("\n");
    *out = NIL_VAL;
    return true;
}

static bool inputNative(VM *vm, int argCount, Value *args, Value *out) {
    (void)vm; (void)argCount; (void)args;
    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        *out = NIL_VAL;
        return true;
    }
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
        len--;
    }
    *out = OBJ_VAL(copyString(vm, buffer, len));
    return true;
}

static bool sinNative(VM *vm, int argCount, Value *args, Value *out) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "sin() expects a Number.");
        return false;
    }
    *out = NUMBER_VAL(sin(AS_NUMBER(args[0])));
    return true;
}

static bool cosNative(VM *vm, int argCount, Value *args, Value *out) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "cos() expects a Number.");
        return false;
    }
    *out = NUMBER_VAL(cos(AS_NUMBER(args[0])));
    return true;
}

static bool tanNative(VM *vm, int argCount, Value *args, Value *out) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "tan() expects a Number.");
        return false;
    }
    *out = NUMBER_VAL(tan(AS_NUMBER(args[0])));
    return true;
}

static bool sqrtNative(VM *vm, int argCount, Value *args, Value *out) {
    (void)vm; (void)argCount;
    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "sqrt() expects a Number.");
        return false;
    }
    *out = NUMBER_VAL(sqrt(AS_NUMBER(args[0])));
    return true;
}

static void defineNativeWithParam(VM *vm, const char *name, NativeFn fn,
                                   const char *returnType, const char *paramType) {
    TypeArray params;
    initTypeArray(&params);
    appendTypeArray(vm, &params, type(vm, paramType));
    defineNative(vm, name, fn, type(vm, returnType), &params);
    freeTypeArray(vm, &params);
}

static void defineBuiltinNatives(VM *vm) {
    defineNative(vm, "clock", clockNative, type(vm, "Number"), NULL);
    defineNativeWithParam(vm, "print", printNative, "Nil", "Any");
    defineNative(vm, "input", inputNative, type(vm, "String"), NULL);

    // Math functions
    defineNativeWithParam(vm, "sin", sinNative, "Number", "Number");
    defineNativeWithParam(vm, "cos", cosNative, "Number", "Number");
    defineNativeWithParam(vm, "tan", tanNative, "Number", "Number");
    defineNativeWithParam(vm, "sqrt", sqrtNative, "Number", "Number");
}

void initVM(VM *vm) {
    vm->chunk = NULL;
    vm->objects = NULL;
    vm->parser = NULL;
    vm->nativeCount = 0;
    vm->grayCount = 0;
    vm->grayCapacity = 0;
    vm->grayStack = NULL;
    vm->nextGC = 1024 * 1024;
    vm->canGC = false;
    resetStack(vm);
    defineBuiltinNatives(vm);
}

void defineNative(VM *vm, const char *name, NativeFn fn,
                  Type *returnType, TypeArray *params) {
    if (vm->nativeCount == NATIVES_MAX) {
        fprintf(stderr, "Too many native functions registered.\n");
        exit(70);
    }
    TypeArray empty;
    if (params == NULL) {
        initTypeArray(&empty);
        params = &empty;
    }
    // Store and publish the native before building its type: functionType()
    // allocates, and the native must already be a root if that triggers a GC.
    int idx = vm->nativeCount;
    vm->natives[idx] = newNative(vm, fn, name);
    vm->nativeCount++;
    vm->nativeTypes[idx] = functionType(vm, returnType, params);
    if (params == &empty) freeTypeArray(vm, &empty);
}

void freeVM(VM *vm) {
    freeObjects(vm, vm->objects);
    // Free all the native types
    for (int i = 0; i < vm->nativeCount; i++) {
        freeType(vm, vm->nativeTypes[i]);
    }
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

static bool call(VM *vm, ObjFunction *function, int argCount) {
    if (argCount != function->prototype->paramaters.count) {
        runtimeError(vm, "Expected %d arguments but got %d.", function->prototype->paramaters.count, argCount);
        return false;
    }

    if (vm->frameCount == FRAMES_MAX) {
        runtimeError(vm, "Stack overflow.");
        return false;
    }

    CallFrame *frame = &vm->frames[vm->frameCount++];
    frame->function = function;
    frame->ip = function->prototype->chunk.code.data;
    frame->slots = vm->stackTop - argCount - 1; // -1 for the callee

    return true;
}

static bool callValue(VM *vm, Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_FUNCTION: {
                return call(vm, AS_FUNCTION(callee), argCount);
            }
            case OBJ_NATIVE: {
                ObjNative *native = AS_NATIVE(callee);
                Value result;
                bool succeeded = native->function(vm, argCount, vm->stackTop - argCount, &result);
                vm->stackTop -= argCount + 1; // Pop arguments and the callee
                push(vm, result);
                return succeeded;
            }
            case OBJ_CLASS: {
                ObjClass* class = AS_CLASS(callee);
                vm->stackTop[-argCount - 1] = OBJ_VAL(newInstance(vm, class));
                return true;
            }
            default:
                break; // Non-callable object type
        }
    }
    runtimeError(vm, "Can only call functions and classes.");
    return false;
}

static ObjUpvalue *captureUpvalue(VM *vm, Value *local) {
    ObjUpvalue *prevUpvalue = NULL;
    ObjUpvalue *upvalue = vm->openUpvalues;

    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue *createdUpvalue = newUpvalue(vm, local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm->openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(VM *vm, Value *last) {
    while (vm->openUpvalues != NULL && vm->openUpvalues->location >= last) {
        ObjUpvalue *upvalue = vm->openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->openUpvalues = upvalue->next;
    }
}

static void defineMethod(VM *vm) {
  Value method = peek(vm, 0);
  ObjClass* class = AS_CLASS(peek(vm, 1));
  appendValueArray(vm, &class->methods, method);
  pop(vm);
}

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static InterpretResult run(VM *vm) {
    CallFrame *frame = &vm->frames[vm->frameCount - 1];
#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() (frame->function->prototype->chunk.constants.data[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define READ_SHORT() ((frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1])))

#define BINARY_OP(vm, valueType, op) \
    do { \
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

        disassembleInstruction(&frame->function->prototype->chunk, (int)(frame->ip - frame->function->prototype->chunk.code.data));
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
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(vm, *frame->function->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->function->upvalues[slot]->location = peek(vm, 0);
                break;
            }
            case OP_CLOSE_UPVALUE: {
                closeUpvalues(vm, vm->stackTop - 1);
                pop(vm);
                break;
            }
            case OP_GET_FIELD: {
                ObjInstance *instance = AS_INSTANCE(peek(vm, 0));
                uint8_t id = READ_BYTE();

                pop(vm);
                push(vm, instance->fields[id]);
                break;
            }
            case OP_SET_FIELD: {
                ObjInstance *instance = AS_INSTANCE(peek(vm, 1));
                uint8_t id = READ_BYTE();
                Value value = pop(vm);
                instance->fields[id] = value;
                pop(vm);
                push(vm, value);
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
                    // Leave the operands on the stack while allocating so a GC
                    // triggered mid-concatenation can't collect them.
                    ObjString *b = AS_STRING(peek(vm, 0));
                    ObjString *a = AS_STRING(peek(vm, 1));
                    int length = a->length + b->length;
                    char *chars = ALLOCATE(vm, char, length + 1);
                    memcpy(chars, a->chars, a->length);
                    memcpy(chars + a->length, b->chars, b->length);
                    chars[length] = '\0';
                    ObjString *result = allocateString(vm, chars, length);
                    pop(vm);
                    pop(vm);
                    push(vm, OBJ_VAL(result));
                } else if (IS_NUMBER(peek(vm, 0)) && IS_NUMBER(peek(vm, 1))) {
                    double b = AS_NUMBER(pop(vm));
                    double a = AS_NUMBER(pop(vm));
                    push(vm, NUMBER_VAL(a + b));
                } else {
                    // unreachable
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
            case OP_CLOSURE: {
                ObjPrototype *prototype = AS_PROTOTYPE(READ_CONSTANT());
                ObjFunction *function = newFunction(vm, prototype);
                push(vm, OBJ_VAL(function));
                for (int i = 0; i< function->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        function->upvalues[i] = captureUpvalue(vm, frame->slots + index);
                    } else {
                        function->upvalues[i] = frame->function->upvalues[index];
                    }
                }
                break;
            }
            case OP_HALT:
                // REPL pause: leave the stack exactly as-is (top-level locals
                // and the function in slot 0 stay live) so the next line can
                // resume against them. No frame teardown, no stack unwind.
                return INTERPRET_OK;
            case OP_RETURN: {
                Value result = pop(vm);
                closeUpvalues(vm, frame->slots);
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
            case OP_CLASS: {
                ObjString *name = READ_STRING();
                uint8_t fieldCount = READ_BYTE();
                push(vm, OBJ_VAL(newClass(vm, name, fieldCount)));
                break;
            }
            case OP_METHOD:
                defineMethod(vm);
                break;
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
    // Keep GC off through compilation; the type checker leaves transient
    // ObjStrings unrooted on the C stack. Re-armed below, before execution.
    vm->canGC = false;

    ObjPrototype *prototype = compile(vm, source);
    if (prototype == NULL) return INTERPRET_COMPILE_ERROR;

    vm->source = (char*)source;

    push(vm, OBJ_VAL(prototype));
    ObjFunction *function = newFunction(vm, prototype);
    pop(vm);
    push(vm, OBJ_VAL(function));
    call(vm, function, 0);

    vm->canGC = true;
    return run(vm);
}

// Compile and run a single REPL line against a persistent top-level scope.
// Unlike interpret(), this does NOT reset the stack: locals declared on earlier
// lines stay on the operand stack and remain addressable. compileRepl() reuses
// one long-lived compiler (so name/type resolution sees prior locals) and tells
// us, via baseSlots, how many slots are already live — that's where the new
// line's function object goes (slot 0) and where the stack top must sit before
// execution resumes. The line's chunk ends in OP_HALT, which returns here
// without unwinding, leaving any newly declared locals on the stack for next
// time.
InterpretResult interpretRepl(VM *vm, const char *source) {
    vm->canGC = false;

    int baseSlots = 0;
    ObjFunction *function = compileRepl(vm, source, &baseSlots);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    vm->source = (char*)source;

    // Slot 0 holds the function; slots 1..baseSlots-1 hold locals carried over
    // from previous lines (untouched since the last OP_HALT).
    vm->stack[0] = OBJ_VAL(function);
    vm->stackTop = vm->stack + baseSlots;

    CallFrame *frame = &vm->frames[0];
    frame->function = function;
    frame->ip = function->prototype->chunk.code.data;
    frame->slots = vm->stack;
    vm->frameCount = 1;

    vm->canGC = true;
    InterpretResult result = run(vm);

    if (result != INTERPRET_OK) {
        // A runtime error leaves the stack in an indeterminate shape; drop the
        // whole session so the next line starts from a clean top-level scope
        // rather than reading stale or partial locals.
        resetRepl();
        resetStack(vm);
    }
    return result;
}
