#include "common.h"
#include "chunk.h"
#include "object.h"
#include "vm.h"
#include "compiler.h"
#include "debug.h"

static void resetStack(VM *vm) {
    vm->stackTop = vm->stack;
}

void initVM(VM *vm) {
    vm->chunk = NULL;
    resetStack(vm);
}

void freeVM(VM *vm) {
    
}

void push(VM *vm, Object object) {
    *vm->stackTop = object;
    vm->stackTop++;
}

Object pop(VM *vm) {
    vm->stackTop--;
    return *vm->stackTop;
}

static InterpretResult run(VM *vm) {
#define READ_BYTE() (*vm->ip++)
#define READ_CONSTANT() (vm->chunk->constants.data[READ_BYTE()])

#define BINARY_OP(op) \
    do { \
        double b = pop(vm); \
        double a = pop(vm); \
        push(vm, a op b); \
    } while (false)

    while (true) {
#ifdef SLC_DEBUG
        printf("      [ ");
        for (Object *slot = vm->stack; slot < vm->stackTop; slot++) {
            printf("[");
            printObject(*slot);
            printf("]");
        }
        printf(" ]\n ");

        disassembleInstruction(vm->chunk, (int)(vm->ip - vm->chunk->code.data));
#endif
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Object constant = READ_CONSTANT();
                push(vm, constant);
                break;
            }
            case OP_ADD: BINARY_OP(+); break;
            case OP_SUBTRACT: BINARY_OP(-); break;
            case OP_MULTIPLY: BINARY_OP(*); break;
            case OP_DIVIDE: BINARY_OP(/); break;
            case OP_NEGATE: push(vm, -pop(vm)); break;
            case OP_RETURN: {
                printObject(pop(vm));
                printf("\n");
                return INTERPRET_OK;
            }
        }
    }

#undef READ_BYTE
#undef READ_CONSTANT

#undef BINARY_OP
}

InterpretResult interpret(VM *vm, const char *source) {
    compile(source);
    return INTERPRET_OK;
}
