#include "common.h"
#include "compiler.h"
#include "lexer.h"
#include "memory.h"
#include "type.h"
#include "value.h"
#include "vm.h"
#ifdef SLC_DEBUG
#include "debug.h"
#endif

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
} Prescedence;

typedef struct {
    Token name;
    Type *type;
    int depth;
    bool isCaptured;
} Local;

typedef struct {
    uint8_t index;
    bool isLocal;
    Type *type;
} Upvalue;

typedef struct {
    ObjString *name;
    uint8_t index;
    Type *type;
} Field;

typedef struct { // Name fields so dynamic array can be swapped in easily.
    Field data[UINT8_COUNT];
    int count;
} FieldList;

typedef struct {
    ObjString *name;
    uint8_t index;
    Type *type;
} Method;

typedef struct { // Name fields so dynamic array can be swapped in easily.
    Method data[UINT8_COUNT];
    int count;
} MethodList;

typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT
} FunctionType;

typedef struct {
    FieldList fields;
    MethodList methods;
} TypeInfo;

MAKE_TABLE_H(TypeTable, TypeTableEntry, Type*, TypeInfo, hashType, typesEqual)
MAKE_TABLE(TypeTable, TypeTableEntry, Type*, TypeInfo, hashType, typesEqual)

typedef struct Compiler {
    struct Compiler *enclosing;
    ObjPrototype *function;
    FunctionType type;

    Local locals[UINT8_COUNT];
    int localCount;
    int scopeDepth;

    Upvalue upvalues[UINT8_COUNT];

    TypeTable *types;
} Compiler;

typedef struct ClassCompiler {
    struct ClassCompiler *enclosing;
    FieldList fields;
    MethodList methods;
} ClassCompiler;

typedef struct Parser {
    Token current;
    Token previous;
    Lexer *lexer;
    Compiler *currentCompiler;
    ClassCompiler *currentClass;
    Chunk *compilingChunk;
    VM *vm;
    Type *prevType;
    bool hadError;
    bool panicMode;
} Parser;

typedef Type *(*ParseFn)(Parser* parser, bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Prescedence prescedence;
} ParseRule;

static Chunk *currentChunk(Parser *parser) {
    return &parser->currentCompiler->function->chunk;
}

#define NIL_TYPE type(parser->vm, "Nil")


static void errorAtKind(Parser *parser, Token *token, const char *kind, const char *message) {
    if (parser->panicMode) return;
    parser->panicMode = true;
    if (token->type != TOKEN_ERROR)
        fprintf(stderr, "\033[31m%s: %s \033[0m\n", kind, message);
    else
        fprintf(stderr, "\033[31m%s: %s \033[0m\n", kind, token->start);
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, "\033[33m at end\033[0m");
    } else if (token->type == TOKEN_ERROR) {

    } else {
        fprintf(stderr, "\033[33m at line %d\033[0m", token->line);
    }
    fprintf(stderr, "\n");

    parser->hadError = true;
}

static void errorAt(Parser *parser, Token *token, const char *message) {
    errorAtKind(parser, token, "SyntaxError", message);
}

static void errorAtCurrent(Parser *parser, const char *message) {
    errorAt(parser, &parser->current, message);
}

static void error(Parser *parser, const char *message) {
    errorAt(parser, &parser->previous, message);
}

static void typeError(Parser *parser, const char *message) {
    errorAtKind(parser, &parser->previous, "TypeError", message);
}


static void advance(Parser *parser) {
    parser->previous = parser->current;

    while (true) {
        parser->current = scanToken(parser->lexer);
        if (parser->current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser, parser->current.start);
    }
}

static void consume(Parser *parser, TokenType type, const char *message) {
    if (parser->current.type == type) {
        advance(parser);
        return;
    }

    errorAtCurrent(parser, message);
}

static bool check(Parser *parser, TokenType type) {
    return parser->current.type == type;
}

static bool match(Parser *parser, TokenType type) {
    if (!check(parser, type)) return false;
    advance(parser);
    return true;
}


static void emitByte(Parser *parser, uint8_t byte) {
    writeChunk(parser->vm, currentChunk(parser), byte, parser->previous.line);
}

static void emitBytes(Parser *parser, uint8_t byte1, uint8_t byte2) {
    emitByte(parser, byte1);
    emitByte(parser, byte2);
}

static void emitLoop(Parser *parser, int loopStart) {
    emitByte(parser, OP_LOOP);

    int offset = currentChunk(parser)->code.count - loopStart + 2;
    if (offset > UINT16_MAX) error(parser, "Loop body too large.");

    emitBytes(parser, (offset >> 8) & 0xff, offset & 0xff);
}

static int emitJump(Parser *parser, uint8_t instruction) {
    emitByte(parser, instruction);
    emitBytes(parser, 0xff, 0xff);
    return currentChunk(parser)->code.count-2;
}

static uint8_t makeConstant(Parser *parser, Value value) {
    int constant = addConstant(parser->vm, currentChunk(parser), value);
    if (constant > UINT8_MAX) {
        error(parser, "Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

static void emitConstant(Parser *parser, Value value) {
    emitBytes(parser, OP_CONSTANT, makeConstant(parser, value));
}

static void initCompiler(Compiler *compiler, Parser *parser, FunctionType t) {
    compiler->enclosing = parser->currentCompiler;
    compiler->function = newPrototype(parser->vm);
    compiler->type = t;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->types = ALLOCATE(parser->vm, TypeTable, 1);
    initTypeTable(compiler->types);

    if (t != TYPE_SCRIPT) {
        compiler->function->name = copyString(parser->vm, parser->previous.start, parser->previous.length);
    }

    Local *local = &compiler->locals[compiler->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    local->name.start = "";
    local->name.length = 0;
    // Give the reserved slot a real type so markReplRoots can mark it safely.
    local->type = type(parser->vm, "Any - Init");

    if (t != TYPE_SCRIPT) {
        local->name = parser->previous;
    }
}

static void patchJump(Parser *parser, int offset) {
    int jump = currentChunk(parser)->code.count - offset - 2;

    if (jump > UINT16_MAX) {
        error(parser, "Too much code to jump over.");
    }

    currentChunk(parser)->code.data[offset] = (jump >> 8) & 0xff;
    currentChunk(parser)->code.data[offset + 1] = jump & 0xff;
}


static ObjPrototype *endCompiler(Parser *parser) {
    emitByte(parser, OP_RETURN);
    ObjPrototype *function = parser->currentCompiler->function;

    if (parser->currentCompiler->enclosing != NULL) {
        parser->currentCompiler = parser->currentCompiler->enclosing;
    } else {
        parser->currentCompiler = NULL;
    }
    return function;
}

static void beginScope(Parser *parser) {
    parser->currentCompiler->scopeDepth++;
}

// Closes the current scope.
static void endScope(Parser *parser) {
    Compiler *c = parser->currentCompiler;
    c->scopeDepth--;
    while (c->localCount > 0 &&
           c->locals[c->localCount - 1].depth > c->scopeDepth) {
        if (parser->currentCompiler->locals[parser->currentCompiler->localCount - 1].isCaptured) {
            emitByte(parser, OP_CLOSE_UPVALUE);
        } else {
            emitByte(parser, OP_POP);
        }
        c->localCount--;
    }
}


static Type *expression(Parser *parser);
static ParseRule *getRule(TokenType type);
static Type *parsePrescedence(Parser *parser, Prescedence prescedence);

static bool isErrorType(Parser *parser, Type *t) {
    return typesEqual(t, errorType(parser->vm));
}

// Writes a human-readable rendering of `t` into buf, joining variants with " | ".
// Returns the number of bytes written (excluding terminator), clamped to bufSize.
static int formatType(Type *t, char *buf, int bufSize) {
    int written = 0;
    for (Type *cur = t; cur != NULL && written < bufSize - 1; cur = cur->next) {
        const char *name = cur->name == NULL ? "<unknown>" : cur->name->chars;
        const char *sep = (cur == t) ? "" : " | ";
        int n = snprintf(buf + written, bufSize - written, "%s%s", sep, name);
        if (n < 0) break;
        written += n;
    }
    return written;
}

static void typeMismatch(Parser *parser, Type *expected, Type *actual, const char *context) {
    if (isErrorType(parser, expected) || isErrorType(parser, actual)) return;
    char expectedBuf[128], actualBuf[128], msg[384];
    formatType(expected, expectedBuf, sizeof(expectedBuf));
    formatType(actual, actualBuf, sizeof(actualBuf));
    snprintf(msg, sizeof(msg), "expected %s in %s, got %s",
             expectedBuf, context, actualBuf);
    typeError(parser, msg);
}

static void expectNumber(Parser *parser, Type *actual, const char *context) {
    if (isErrorType(parser, actual)) return;
    Type *numberT = type(parser->vm, "Number");
    if (!typesEqual(actual, numberT)) {
        typeMismatch(parser, numberT, actual, context);
    }
}


static bool identifiersEqual(Token *a, Token *b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

// Add a local with an "uninitialized" depth sentinel (-1).
static int addLocal(Parser *parser, Token name, Type *type) {
    Compiler *compiler = parser->currentCompiler;
    if (compiler->localCount == UINT8_COUNT) {
        error(parser, "Too many local variables in function.");
        return -1;
    }

    int slot = compiler->localCount;
    Local *local = &compiler->locals[compiler->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
    local->type = type;
    return slot;
}

static int declareVariable(Parser *parser, Token name, Type *type) {
    Compiler *compiler = parser->currentCompiler;
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local *local = &compiler->locals[i];
        if (local->depth != -1 && local->depth < compiler->scopeDepth) break;
        if (identifiersEqual(&name, &local->name)) {
            error(parser, "Already a variable with this name in this scope.");
        }
    }
    return addLocal(parser, name, type);
}

static void markInitializedAt(Parser *parser, int slot) {
    Compiler *compiler = parser->currentCompiler;
    compiler->locals[slot].depth = compiler->scopeDepth;
}

static void markInitialized(Parser *parser) {
    markInitializedAt(parser, parser->currentCompiler->localCount - 1);
}

static void setLocalType(Parser *parser, int slot, Type *type) {
    Compiler *compiler = parser->currentCompiler;
    compiler->locals[slot].type = type;
}

static int resolveLocal(Parser *parser, Compiler *compiler, Token *name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local *local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error(parser, "Can't read local variable in its own initializer.");
            }
            return i;
        }
    }
    return -1;
}

static int addUpvalue(Parser *parser, Compiler *compiler, uint8_t index, Type *type, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++) {
        Upvalue *upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT) {
        error(parser, "Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].index = index;
    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].type = type;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Parser *parser, Compiler *compiler, Token *name) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolveLocal(parser, compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(parser, compiler, (uint8_t)local, compiler->enclosing->locals[local].type, true);
    }

    int upvalue = resolveUpvalue(parser, compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(parser, compiler, (uint8_t)upvalue, compiler->enclosing->upvalues[upvalue].type, false);
    }

    return -1;
}

static int resolveNative(Parser *parser, Token *name) {
    VM *vm = parser->vm;
    for (int i = 0; i < vm->nativeCount; i++) {
        const char *nName = vm->natives[i]->name;
        size_t nLen = strlen(nName);
        if ((int)nLen == name->length && memcmp(nName, name->start, nLen) == 0) {
            return i;
        }
    }
    return -1;
}

static uint8_t findPropertyId(Parser *parser, Token name, Type *type) {
    TypeInfo *fields = ALLOCATE(parser->vm, TypeInfo, 1);
    
    if (getTypeTable(parser->currentCompiler->types, type, fields)) {
        for (int i = 0; i < fields->fields.count; i++) {
            Field *field = &fields->fields.data[i];
            if (name.length == field->name->length && memcmp(name.start, field->name->chars, name.length) == 0) {
                return field->index;
            }
        }
        return 255;
    } else {
        return 255;
    }
}

static uint8_t findMethodId(Parser *parser, Token name, Type *type) {
    TypeInfo *fields = ALLOCATE(parser->vm, TypeInfo, 1);
    
    if (getTypeTable(parser->currentCompiler->types, type, fields)) {
        for (int i = 0; i < fields->methods.count; i++) {
            Method *method = &fields->methods.data[i];
            if (name.length == method->name->length && memcmp(name.start, method->name->chars, name.length) == 0) {
                return method->index;
            }
        }
        return 255;
    } else {
        return 255;
    }
}


static Type *binary(Parser *parser, bool canAssign) {
    TokenType operatorType = parser->previous.type;
    Type *leftType = parser->prevType;
    ParseRule* rule = getRule(operatorType);
    Type *rightType = parsePrescedence(parser, (Prescedence)(rule->prescedence + 1));

    Type *numberT = type(parser->vm, "Number");
    Type *stringT = type(parser->vm, "String");
    Type *boolT = type(parser->vm, "Boolean");

    switch (operatorType) {
        case TOKEN_BANG_EQUAL: emitBytes(parser, OP_EQUAL, OP_NOT); return boolT;
        case TOKEN_EQUAL_EQUAL: emitByte(parser, OP_EQUAL); return boolT;
        case TOKEN_GREATER:
            expectNumber(parser, leftType, "left operand of '>'");
            expectNumber(parser, rightType, "right operand of '>'");
            emitByte(parser, OP_GREATER); return boolT;
        case TOKEN_GREATER_EQUAL:
            expectNumber(parser, leftType, "left operand of '>='");
            expectNumber(parser, rightType, "right operand of '>='");
            emitByte(parser, OP_GREATER_EQUAL); return boolT;
        case TOKEN_LESS:
            expectNumber(parser, leftType, "left operand of '<'");
            expectNumber(parser, rightType, "right operand of '<'");
            emitByte(parser, OP_LESS); return boolT;
        case TOKEN_LESS_EQUAL:
            expectNumber(parser, leftType, "left operand of '<='");
            expectNumber(parser, rightType, "right operand of '<='");
            emitByte(parser, OP_LESS_EQUAL); return boolT;
        case TOKEN_PLUS:
            emitByte(parser, OP_ADD);
            if (typesEqual(leftType, numberT) && typesEqual(rightType, numberT)) return numberT;
            if (typesEqual(leftType, stringT) && typesEqual(rightType, stringT)) return stringT;
            if (!isErrorType(parser, leftType) && !isErrorType(parser, rightType)) {
                typeError(parser, "operator '+' requires two Numbers or two Strings");
            }
            return errorType(parser->vm);
        case TOKEN_MINUS:
            expectNumber(parser, leftType, "left operand of '-'");
            expectNumber(parser, rightType, "right operand of '-'");
            emitByte(parser, OP_SUBTRACT); return numberT;
        case TOKEN_STAR:
            expectNumber(parser, leftType, "left operand of '*'");
            expectNumber(parser, rightType, "right operand of '*'");
            emitByte(parser, OP_MULTIPLY); return numberT;
        case TOKEN_SLASH:
            expectNumber(parser, leftType, "left operand of '/'");
            expectNumber(parser, rightType, "right operand of '/'");
            emitByte(parser, OP_DIVIDE); return numberT;
        default: return errorType(parser->vm); // Unreachable.
    }
}

static Type *literal(Parser *parser, bool canAssign) {
    switch (parser->previous.type) {
        case TOKEN_FALSE: emitByte(parser, OP_FALSE); return type(parser->vm, "Boolean");
        case TOKEN_NIL: emitByte(parser, OP_NIL); return NIL_TYPE;
        case TOKEN_TRUE: emitByte(parser, OP_TRUE); return type(parser->vm, "Boolean");
        default: return errorType(parser->vm);
    }
}

static Type *grouping(Parser *parser, bool canAssign) {
    Type *type = expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
    return type;
}

static Type *number(Parser *parser, bool canAssign) {
    double value = strtod(parser->previous.start, NULL);
    emitConstant(parser, NUMBER_VAL(value));
    return type(parser->vm, "Number");
}

static Type *string(Parser *parser, bool canAssign) {
    emitConstant(parser, OBJ_VAL(copyString(parser->vm, parser->previous.start + 1, parser->previous.length - 2)));
    return type(parser->vm, "String");
}

static Type *parseType(Parser *parser) {
    consume(parser, TOKEN_IDENTIFIER, "Expect type name.");
    Type *type = tokenType(parser->vm, parser->previous);
    while (match(parser, TOKEN_PIPE)) {
        consume(parser, TOKEN_IDENTIFIER, "Expect type name after pipe.");
        type = unionType(parser->vm, type, tokenType(parser->vm, parser->previous));
    }
    return type;
}

static Type *namedVariable(Parser *parser, Token name, bool canAssign) {
    Compiler *compiler = parser->currentCompiler;

    if (match(parser, TOKEN_COLON)) {
        Type *declaredType = parseType(parser);
        consume(parser, TOKEN_EQUAL, "Expect value for variable after type annotation.");
        // Declare before the initializer so a self-reference resolves to this
        // local with depth=-1 and is rejected by resolveLocal.
        int slot = declareVariable(parser, name, declaredType);
        Type *valueType = expression(parser);
        if (!isSubtype(valueType, declaredType)) {
            typeMismatch(parser, declaredType, valueType, "variable declaration");
        }
        markInitializedAt(parser, slot);
        emitBytes(parser, OP_GET_LOCAL, (uint8_t)slot);
        return declaredType;
    }

    int arg = resolveLocal(parser, parser->currentCompiler, &name);

    if (canAssign && match(parser, TOKEN_EQUAL)) {
        if (arg == -1) {
            // Implicit declaration: type is inferred from the initializer, so
            // we declare with a neutral placeholder, evaluate, then patch the
            // slot's type. Declaring first still gives us the self-init check.
            int slot = declareVariable(parser, name, type(parser->vm, "Any - Var"));
            Type *valueType = expression(parser);
            setLocalType(parser, slot, valueType);
            markInitializedAt(parser, slot);
            emitBytes(parser, OP_GET_LOCAL, (uint8_t)slot);
            return valueType;
        }
        Type *valueType = expression(parser);
        Type *existingType = compiler->locals[arg].type;
        if (!isSubtype(valueType, existingType)) {
            typeMismatch(parser, existingType, valueType, "assignment");
        }
        emitBytes(parser, OP_SET_LOCAL, (uint8_t)arg);
        return existingType;
    }

    if (arg != -1) {
        emitBytes(parser, OP_GET_LOCAL, (uint8_t)arg);
        return compiler->locals[arg].type;
    } else if ((arg = resolveUpvalue(parser, parser->currentCompiler, &name)) != -1) {
        emitBytes(parser, OP_GET_UPVALUE, (uint8_t)arg);
        return parser->currentCompiler->upvalues[arg].type;
    }

    int nativeIdx = resolveNative(parser, &name);
    if (nativeIdx != -1) {
        emitBytes(parser, OP_GET_NATIVE, (uint8_t)nativeIdx);
        return parser->vm->nativeTypes[nativeIdx];
    }

    error(parser, "Undefined variable.");
    return errorType(parser->vm);
}

static Type *variable(Parser *parser, bool canAssign) {
    return namedVariable(parser, parser->previous, canAssign);
}

// `outer x = expr` assigns to the nearest variable named `x` in an enclosing
// function scope (an upvalue), Solace's analogue of Python's `nonlocal`:
// it is the only path that writes through a captured upvalue. 
static Type *outerVariable(Parser *parser, bool canAssign) {
    consume(parser, TOKEN_IDENTIFIER, "Expect variable name after 'outer'.");
    Token name = parser->previous;

    int arg = resolveUpvalue(parser, parser->currentCompiler, &name);
    if (arg == -1) {
        error(parser, "No variable with this name in an outer scope.");
        // Still consume the assignment so we don't cascade into bogus errors.
        if (match(parser, TOKEN_EQUAL)) expression(parser);
        return errorType(parser->vm);
    }

    consume(parser, TOKEN_EQUAL, "Expect '=' after 'outer' variable.");
    Type *valueType = expression(parser);
    Type *existingType = parser->currentCompiler->upvalues[arg].type;
    if (!isSubtype(valueType, existingType)) {
        typeMismatch(parser, existingType, valueType, "outer assignment");
    }
    emitBytes(parser, OP_SET_UPVALUE, (uint8_t)arg);
    return existingType;
}

static Type *function(Parser *parser, FunctionType funType) {
    Compiler compiler;
    initCompiler(&compiler, parser, funType);
    parser->currentCompiler = &compiler;
    beginScope(parser);

    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            if (compiler.function->paramaters.count == UINT8_COUNT) {
                error(parser, "Can't have more than 255 parameters.");
                return errorType(parser->vm);
            }
            consume(parser, TOKEN_IDENTIFIER, "Expect parameter name.");
            Token paramName = parser->previous;

            Type *paramType = NIL_TYPE;
            if (match(parser, TOKEN_COLON)) {
                paramType = parseType(parser);
            } else {
                paramType = type(parser->vm, "Any - Param");
            }

            declareVariable(parser, paramName, paramType);
            markInitialized(parser);

            appendTypeArray(parser->vm, &compiler.function->paramaters, paramType);
        } while (match(parser, TOKEN_COMMA));
    }
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

    if (match(parser, TOKEN_GREATER)) {
        compiler.function->returnType = parseType(parser);
    } else {
        compiler.function->returnType = type(parser->vm, "Any - Return");
    }

    Type *funcType = functionType(parser->vm, compiler.function->returnType,
                                 &compiler.function->paramaters);
    compiler.locals[0].type = funcType;

    while (!check(parser, TOKEN_END) && !parser->hadError) {
        expression(parser);
        if (!check(parser, TOKEN_END)) emitByte(parser, OP_POP);
    }
    emitByte(parser, OP_RETURN);

    consume(parser, TOKEN_END, "Expect 'end' after function body.");

    ObjPrototype *functionObj = endCompiler(parser);
    emitBytes(parser, OP_CLOSURE, makeConstant(parser, OBJ_VAL(functionObj)));

    for (int i = 0; i < functionObj->upvalueCount; i++) {
        emitBytes(parser, compiler.upvalues[i].isLocal ? 1 : 0, compiler.upvalues[i].index);
    }
    return funcType;
}

static void method(Parser *parser, ClassCompiler *classCompiler) {
    consume(parser, TOKEN_IDENTIFIER, "Expect method name.");
    Token methodName = parser->previous;

    FunctionType type = TYPE_FUNCTION;
    Type *functionType = function(parser, type);
    classCompiler->methods.data[classCompiler->methods.count].name = copyString(parser->vm, methodName.start, methodName.length);
    classCompiler->methods.data[classCompiler->methods.count].type = functionType;
    classCompiler->methods.data[classCompiler->methods.count].index = classCompiler->methods.count;
    classCompiler->methods.count++;

    emitByte(parser, OP_METHOD);
}


// Coerce the value on top of the stack to a real Boolean.
static void emitCoerceBool(Parser *parser) {
    emitBytes(parser, OP_NOT, OP_NOT);
}

static Type *and_(Parser *parser, bool canAssign) {
    emitCoerceBool(parser);                              // coerce left
    int endJump = emitJump(parser, OP_JUMP_IF_FALSE);

    emitByte(parser, OP_POP);
    parsePrescedence(parser, PREC_AND);
    emitCoerceBool(parser);                              // coerce right

    patchJump(parser, endJump);
    return type(parser->vm, "Boolean");
}

static Type *or_(Parser *parser, bool canAssign) {
    emitCoerceBool(parser);                              // coerce left
    int elseJump = emitJump(parser, OP_JUMP_IF_FALSE);
    int endJump = emitJump(parser, OP_JUMP);

    patchJump(parser, elseJump);
    emitByte(parser, OP_POP);


    parsePrescedence(parser, PREC_OR);
    emitCoerceBool(parser);                              // coerce right
    patchJump(parser, endJump);
    return type(parser->vm, "Boolean");
}

static Type *unary(Parser *parser, bool canAssign) {
    TokenType operatorType = parser->previous.type;

    Type *operandType = parsePrescedence(parser, PREC_UNARY);

    switch (operatorType) {
        case TOKEN_NOT: emitByte(parser, OP_NOT); return type(parser->vm, "Boolean");
        case TOKEN_MINUS:
            expectNumber(parser, operandType, "operand of unary '-'");
            emitByte(parser, OP_NEGATE);
            return type(parser->vm, "Number");
        default: return errorType(parser->vm);
    }
}

static Type *retExpr(Parser *parser, bool canAssign) {
    if (parser->currentCompiler->type == TYPE_SCRIPT) {
        error(parser, "Can't return from top-level code.");
        return errorType(parser->vm);
    }

    Type *valueType = expression(parser);
    Type *expected = parser->currentCompiler->function->returnType;
    if (!isSubtype(valueType, expected)) {
        typeMismatch(parser, expected, valueType, "return value");
    }
    emitByte(parser, OP_RETURN);
    return valueType;
}

static Type *call(Parser *parser, bool canAssign) {
    // Snapshot callee type before argumentList — expression() inside the loop
    // will clobber parser->prevType with each argument's type.
    Type *calleeType = parser->prevType;

    Type *retSlot = NULL;
    Type *firstParamSlot = NULL;
    bool fnTyped = isFunctionType(*calleeType);
    if (fnTyped) {
        if (calleeType->generics != NULL) {
            retSlot = calleeType->generics;
            firstParamSlot = retSlot->next;
        }
    } else if (isClassType(*calleeType)) {
        retSlot = calleeType->generics;
    } else if (!isErrorType(parser, calleeType)) {
        char buf[128], msg[256];
        formatType(calleeType, buf, sizeof(buf));
        snprintf(msg, sizeof(msg), "expected callable type, got %s", buf);
        typeError(parser, msg);
    }

    uint8_t argCount = 0;
    Type *paramSlot = firstParamSlot;
    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            Type *argType = expression(parser);
            if (fnTyped && paramSlot != NULL && paramSlot->generics != NULL) {
                if (!isSubtype(argType, paramSlot->generics)) {
                    typeMismatch(parser, paramSlot->generics, argType, "function argument");
                }
                paramSlot = paramSlot->next;
            }
            if (argCount == 255) typeError(parser, "Can't have more than 255 arguments.");
            argCount++;
        } while (match(parser, TOKEN_COMMA));
    }
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");

    if (fnTyped) {
        int paramCount = 0;
        for (Type *s = firstParamSlot; s != NULL; s = s->next) paramCount++;
        if (paramCount != argCount) {
            char msg[128];
            snprintf(msg, sizeof(msg), "expected %d arguments, got %d", paramCount, argCount);
            typeError(parser, msg);
        }
    }

    emitBytes(parser, OP_CALL, argCount);

    if (fnTyped) {
        if (retSlot != NULL && retSlot->generics != NULL) return retSlot->generics;
    } else if (isClassType(*calleeType)) {
        if (retSlot != NULL) return retSlot;
    }
    return errorType(parser->vm);
}

static Type *dot(Parser *parser, bool canAssign) {
    consume(parser, TOKEN_IDENTIFIER, "Expect property name after '.'.");
    

    TypeInfo *fields = ALLOCATE(parser->vm, TypeInfo, 1);
    if (getTypeTable(parser->currentCompiler->types, parser->prevType, fields)) {

    } else {
        typeError(parser, "Only instances have fields.");
        printValue(OBJ_VAL(parser->prevType->name));
        return errorType(parser->vm);
    }
    uint8_t id;
    id = findPropertyId(parser, parser->previous, parser->prevType);
    if (id == 255) {
        id = findMethodId(parser, parser->previous, parser->prevType);
        if (id == 255) {
            typeError(parser, "Unknown field or method.");
            return errorType(parser->vm);
        }
        Type *expectedType = fields->methods.data[id].type;
        if (canAssign && match(parser, TOKEN_EQUAL)) {
            typeError(parser, "Methods cannot be assigned.");
            return errorType(parser->vm);
        }
        emitBytes(parser, OP_GET_METHOD, id);
        return expectedType;
    } else {
        Type *expectedType = fields->fields.data[id].type;

        if (canAssign && match(parser, TOKEN_EQUAL)) {
            Type *valueType = expression(parser);

            if (!typesEqual(valueType, expectedType)) {
                typeMismatch(parser, expectedType, valueType, "field assignment");
                return errorType(parser->vm);
            } else {
                emitBytes(parser, OP_SET_FIELD, id);
                return parser->prevType;
            }
        }

        emitBytes(parser, OP_GET_FIELD, id);
        return expectedType;
    }
}

static Type *ifExpr(Parser *parser, bool canAssign) {
    expression(parser);

    int thenJump = emitJump(parser, OP_JUMP_IF_FALSE);
    emitByte(parser, OP_POP);
    beginScope(parser);
    Type *thenType = NIL_TYPE;
    bool thenEmpty = true;
    while (!check(parser, TOKEN_END) && !check(parser, TOKEN_ELSE) && !parser->hadError) {
        thenType = expression(parser);
        thenEmpty = false;
        if (!check(parser, TOKEN_END) && !check(parser, TOKEN_ELSE)) emitByte(parser, OP_POP);
    }
    if (thenEmpty) emitByte(parser, OP_NIL);  // keep stack balanced when body is empty
    endScope(parser);

    int elseJump = emitJump(parser, OP_JUMP);

    patchJump(parser, thenJump);
    emitByte(parser, OP_POP);

    Type *elseType = NIL_TYPE;
    if (match(parser, TOKEN_ELSE)) {
        beginScope(parser);
        bool elseEmpty = true;
        while (!check(parser, TOKEN_END) && !parser->hadError) {
            elseType = expression(parser);
            elseEmpty = false;
            if (!check(parser, TOKEN_END)) emitByte(parser, OP_POP);
        }
        if (elseEmpty) emitByte(parser, OP_NIL);
        endScope(parser);
        consume(parser, TOKEN_END, "Expected 'end' after else block.");
    } else {
        consume(parser, TOKEN_END, "Expected 'end' after if block.");
        emitByte(parser, OP_NIL);
    }
    patchJump(parser, elseJump);

    return unionType(parser->vm, thenType, elseType);
}

static Type *whileExpr(Parser *parser, bool canAssign) {
    // Initial value on stack for first iteration
    emitByte(parser, OP_NIL);

    int loopStart = currentChunk(parser)->code.count;
    expression(parser);

    int exitJump = emitJump(parser, OP_JUMP_IF_FALSE);
    emitByte(parser, OP_POP);  // pop condition
    emitByte(parser, OP_POP);  // pop previous iteration result

    beginScope(parser);
    Type *lastType = NIL_TYPE;
    bool bodyEmpty = true;
    while (!check(parser, TOKEN_END) && !parser->hadError) {
        lastType = expression(parser);
        bodyEmpty = false;
        if (!check(parser, TOKEN_END)) emitByte(parser, OP_POP);
    }
    if (bodyEmpty) emitByte(parser, OP_NIL);  // keep stack balanced when body is empty
    endScope(parser);
    consume(parser, TOKEN_END, "Expected 'end' after while body.");

    // At this point, body result is on stack
    // Loop back to test condition again with body result still there
    emitLoop(parser, loopStart);

    patchJump(parser, exitJump);
    emitByte(parser, OP_POP);  // pop the false condition
    return lastType;
}

static Type *funExpr(Parser *parser, bool canAssign) {
    consume(parser, TOKEN_IDENTIFIER, "Expect function name.");
    Compiler *outer = parser->currentCompiler;

    int slot = declareVariable(parser, parser->previous, type(parser->vm, "Any - Placeholder"));

    Type *funcType = function(parser, TYPE_FUNCTION);
    outer->locals[slot].type = funcType;
    markInitializedAt(parser, slot);

    emitBytes(parser, OP_GET_LOCAL, (uint8_t)slot);
    return funcType;
}

static Type *class(Parser *parser, bool canAssign) {
    consume(parser, TOKEN_IDENTIFIER, "Expect class name.");

    Type *classNameType = tokenType(parser->vm, parser->previous);
    Type *classType = type(parser->vm, "Class");
    classType->generics = classNameType;

    ClassCompiler classCompiler = {0};
    classCompiler.enclosing = parser->currentClass;
    parser->currentClass = &classCompiler;

    Token className = parser->previous;

    uint8_t nameConstant = makeConstant(parser, OBJ_VAL(copyString(parser->vm, parser->previous.start, parser->previous.length)));
    int slot = declareVariable(parser, parser->previous, classType);
    markInitializedAt(parser, slot);

    emitByte(parser, OP_CLASS);
    emitByte(parser, nameConstant);
    int fieldCountOffset = currentChunk(parser)->code.count;
    emitByte(parser, 0);

    while (!check(parser, TOKEN_END) && !parser->hadError) {
        if (match(parser, TOKEN_IDENTIFIER)) {
            Token name = parser->previous;
            consume(parser, TOKEN_COLON, "Expect type after field name.");
            Type *type = parseType(parser);
            classCompiler.fields.data[classCompiler.fields.count].name = copyString(parser->vm, name.start, name.length);
            classCompiler.fields.data[classCompiler.fields.count].type = type;
            classCompiler.fields.data[classCompiler.fields.count].index = classCompiler.fields.count;
            classCompiler.fields.count++;
        } else {
            consume(parser, TOKEN_DEF, "Expect field name or method definition in class body.");
            method(parser, &classCompiler);
        }
    }

    consume(parser, TOKEN_END, "Expect 'end' after class body.");

    if (classCompiler.fields.count > UINT8_MAX) {
        error(parser, "Class has too many fields.");
        classCompiler.fields.count = UINT8_MAX;
    }
    currentChunk(parser)->code.data[fieldCountOffset] = (uint8_t)classCompiler.fields.count;
    
    namedVariable(parser, className, false);

    TypeInfo typeInfo;
    typeInfo.fields = classCompiler.fields;
    typeInfo.methods = classCompiler.methods;
    setTypeTable(parser->vm, parser->currentCompiler->types, classNameType, typeInfo);

    parser->currentClass = parser->currentClass->enclosing;
    return classType;
}


ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping,      call,   PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL,          NULL,   PREC_NONE},
    [TOKEN_COMMA]         = {NULL,          NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,          dot,    PREC_CALL},
    [TOKEN_MINUS]         = {unary,         binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL,          binary, PREC_TERM},
    [TOKEN_SLASH]         = {NULL,          binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,          binary, PREC_FACTOR},
    [TOKEN_COLON]         = {NULL,          NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,          binary, PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL,          NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,          binary, PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,          binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,          binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,          binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,          binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {variable,      NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,        NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,        NULL,   PREC_NONE},
    [TOKEN_AND]           = {and_,          NULL,   PREC_NONE},
    [TOKEN_CLASS]         = {class,         NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,          NULL,   PREC_NONE},
    [TOKEN_END]           = {NULL,          NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,       NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,          NULL,   PREC_NONE},
    [TOKEN_DEF]           = {funExpr,       NULL,   PREC_NONE},
    [TOKEN_IF]            = {ifExpr,        NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,       NULL,   PREC_NONE},
    [TOKEN_NOT]           = {unary,         NULL,   PREC_NONE},
    [TOKEN_OR]            = {or_,           NULL,   PREC_NONE},
    [TOKEN_OUTER]         = {outerVariable, NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {retExpr,       NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {NULL,          NULL,   PREC_NONE},
    [TOKEN_SELF]          = {NULL,          NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {literal,       NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {whileExpr,     NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,          NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,          NULL,   PREC_NONE},
};


static Type *parsePrescedence(Parser *parser, Prescedence prescedence) {
    advance(parser);
    ParseFn prefixRule = getRule(parser->previous.type)->prefix;
    Type *exprType;
    if (prefixRule == NULL) {
        error(parser, "Expect expression.");
        return errorType(parser->vm);
    }

    bool canAssign = prescedence <= PREC_ASSIGNMENT;
    exprType = prefixRule(parser, canAssign);
    parser->prevType = exprType;

    while (prescedence <= getRule(parser->current.type)->prescedence) {
        advance(parser);
        ParseFn infixRule = getRule(parser->previous.type)->infix;
        exprType = infixRule(parser, canAssign);
        parser->prevType = exprType;
    }

    if (canAssign && match(parser, TOKEN_EQUAL)) {
        error(parser, "Invalid assignment target.");
    }

    return exprType;
}

static ParseRule *getRule(TokenType type) {
    return &rules[type];
}

static Type *expression(Parser *parser) {
    return parsePrescedence(parser, PREC_ASSIGNMENT);
}


// Mark every prototype in the in-flight compiler chain so a GC triggered during
// compilation doesn't collect functions that are still being built.
void markCompilerRoots(VM *vm) {
    if (vm->parser == NULL) return;

    for (Compiler *compiler = vm->parser->currentCompiler;
         compiler != NULL;
         compiler = compiler->enclosing) {
        markObject(vm, (Obj*)compiler->function);
    }
}

static Compiler replCompiler;
static bool replInitialized = false;

static char **replSources = NULL;
static int replSourceCount = 0;
static int replSourceCap = 0;

static char *replRetainSource(const char *source) {
    if (replSourceCount == replSourceCap) {
        replSourceCap = replSourceCap < 8 ? 8 : replSourceCap * 2;
        replSources = realloc(replSources, sizeof(char*) * replSourceCap);
    }
    size_t size = strlen(source) + 1;
    char *copy = malloc(size);
    memcpy(copy, source, size);
    replSources[replSourceCount++] = copy;
    return copy;
}

void markReplRoots(VM *vm) {
    if (!replInitialized) return;
    markObject(vm, (Obj*)replCompiler.function);
    for (int i = 0; i < replCompiler.localCount; i++) {
        markType(vm, replCompiler.locals[i].type);
    }
}

void resetRepl(void) {
    replInitialized = false;
    for (int i = 0; i < replSourceCount; i++) free(replSources[i]);
    free(replSources);
    replSources = NULL;
    replSourceCount = 0;
    replSourceCap = 0;
}

ObjPrototype *compile(VM *vm, const char *source) {
    Lexer lexer;
    initLexer(&lexer, source);
    Parser parser;
    parser.lexer = &lexer;
    parser.hadError = false;
    parser.panicMode = false;
    parser.vm = vm;
    parser.prevType = errorType(vm);
    parser.currentCompiler = NULL;  // initCompiler reads this for `enclosing`
    parser.currentClass = NULL;
    vm->parser = &parser;           // exposes the compiler chain to the GC
    Compiler compiler;
    initCompiler(&compiler, &parser, TYPE_SCRIPT);
    parser.currentCompiler = &compiler;
    
    advance(&parser);
    
    // Top-level expressions, like function/if/while bodies, leave their value
    // on the stack; pop to prevent stack overflow
    while (!match(&parser, TOKEN_EOF) && !parser.hadError) {
        expression(&parser);
        emitByte(&parser, OP_POP);
    }
    
    ObjPrototype *function = endCompiler(&parser);

    vm->parser = NULL;

    #ifdef SLC_DEBUG
    if (!parser.hadError) {
        disassembleChunk(&function->chunk, function->name != NULL ? function->name->chars : "<script>");
    }
    #endif

    return parser.hadError ? NULL : function;
}

ObjFunction *compileRepl(VM *vm, const char *source, int *baseSlots) {
    source = replRetainSource(source);

    Lexer lexer;
    initLexer(&lexer, source);
    Parser parser;
    parser.lexer = &lexer;
    parser.hadError = false;
    parser.panicMode = false;
    parser.vm = vm;
    parser.prevType = errorType(vm);
    parser.currentCompiler = replInitialized ? &replCompiler : NULL;
    vm->parser = &parser;

    if (!replInitialized) {
        initCompiler(&replCompiler, &parser, TYPE_SCRIPT);
        replInitialized = true;
    } else {
        replCompiler.function = newPrototype(vm);
        replCompiler.scopeDepth = 0;
    }
    parser.currentCompiler = &replCompiler;

    int savedLocalCount = replCompiler.localCount;
    *baseSlots = savedLocalCount;

    advance(&parser);
    while (!match(&parser, TOKEN_EOF) && !parser.hadError) {
        expression(&parser);
        emitByte(&parser, OP_POP);
    }
    // Pause rather than return; OP_HALT stops the VM without unwinding
    emitByte(&parser, OP_HALT);

    ObjPrototype *prototype = replCompiler.function;
    vm->parser = NULL;

    if (parser.hadError) {
        // We never ran, so the runtime stack is untouched; roll the persistent
        // scope back to match it by dropping locals this line half-declared.
        replCompiler.localCount = savedLocalCount;
        return NULL;
    }

    #ifdef SLC_DEBUG
    disassembleChunk(&prototype->chunk, "<repl>");
    #endif

    push(vm, OBJ_VAL(prototype));
    ObjFunction *function = newFunction(vm, prototype);
    pop(vm);
    return function;
}
