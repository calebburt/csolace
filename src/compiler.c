#include "common.h"
#include "compiler.h"
#include "lexer.h"
#include "type.h"
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
    Type type;
    int depth;
} Local;

typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT
} FunctionType;

typedef struct Compiler {
    struct Compiler *enclosing;
    ObjPrototype *function;
    FunctionType type;

    Local locals[UINT8_COUNT];
    int localCount;
    int scopeDepth;
} Compiler;

typedef struct {
    Token current;
    Token previous;
    Lexer *lexer;
    Compiler *currentCompiler;
    Chunk *compilingChunk;
    VM *vm;
    Type prevType;
    bool hadError;
    bool panicMode;
} Parser;

typedef Type (*ParseFn)(Parser* parser, bool canAssign);

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
    writeChunk(currentChunk(parser), byte, parser->previous.line);
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
    int constant = addConstant(currentChunk(parser), value);
    if (constant > UINT8_MAX) {
        error(parser, "Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

static void emitConstant(Parser *parser, Value value) {
    emitBytes(parser, OP_CONSTANT, makeConstant(parser, value));
}

static void initCompiler(Compiler *compiler, Parser *parser, FunctionType type) {
    compiler->enclosing = parser->currentCompiler;
    compiler->function = newPrototype(parser->vm);
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;

    if (type != TYPE_SCRIPT) {
        compiler->function->name = copyString(parser->vm, parser->previous.start, parser->previous.length);
    }

    Local *local = &compiler->locals[compiler->localCount++];
    local->depth = 0;
    local->name.start = "";
    local->name.length = 0;

    if (type != TYPE_SCRIPT) {
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

// Closes the current scope. The body of an if/while is an expression whose
// value sits on top of the stack; any locals declared inside that body live
// on the stack just below the result. Without cleanup, each loop iteration
// would leak its body-locals onto the stack, overflowing on the 50th-ish
// iteration. Slide the result down into the first soon-to-be-popped slot,
// then pop the now-redundant values above it.
static void endScope(Parser *parser) {
    Compiler *c = parser->currentCompiler;
    c->scopeDepth--;
    int popCount = 0;
    while (c->localCount > 0 &&
           c->locals[c->localCount - 1].depth > c->scopeDepth) {
        popCount++;
        c->localCount--;
    }
    if (popCount > 0) {
        int firstSlot = c->localCount;
        emitBytes(parser, OP_SET_LOCAL, (uint8_t)firstSlot);
        for (int i = 0; i < popCount; i++) {
            emitByte(parser, OP_POP);
        }
    }
}


static Type expression(Parser *parser);
static ParseRule *getRule(TokenType type);
static Type parsePrescedence(Parser *parser, Prescedence prescedence);

static bool isErrorType(Parser *parser, Type t) {
    return typesEqual(t, errorType(parser->vm));
}

// Writes a human-readable rendering of `t` into buf, joining variants with " | ".
// Returns the number of bytes written (excluding terminator), clamped to bufSize.
static int formatType(Type t, char *buf, int bufSize) {
    int written = 0;
    for (Type *cur = &t; cur != NULL && written < bufSize - 1; cur = cur->next) {
        const char *name = cur->name == NULL ? "<unknown>" : cur->name->chars;
        const char *sep = (cur == &t) ? "" : " | ";
        int n = snprintf(buf + written, bufSize - written, "%s%s", sep, name);
        if (n < 0) break;
        written += n;
    }
    return written;
}

static void typeMismatch(Parser *parser, Type expected, Type actual, const char *context) {
    if (isErrorType(parser, expected) || isErrorType(parser, actual)) return;
    char expectedBuf[128], actualBuf[128], msg[384];
    formatType(expected, expectedBuf, sizeof(expectedBuf));
    formatType(actual, actualBuf, sizeof(actualBuf));
    snprintf(msg, sizeof(msg), "expected %s in %s, got %s",
             expectedBuf, context, actualBuf);
    typeError(parser, msg);
}

static void expectNumber(Parser *parser, Type actual, const char *context) {
    if (isErrorType(parser, actual)) return;
    Type numberT = type(parser->vm, "Number");
    if (!typesEqual(actual, numberT)) {
        typeMismatch(parser, numberT, actual, context);
    }
}


static bool identifiersEqual(Token *a, Token *b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

// Add a local with an "uninitialized" depth sentinel (-1).
static void addLocal(Parser *parser, Token name, Type type) {
    Compiler *compiler = parser->currentCompiler;
    if (compiler->localCount == UINT8_COUNT) {
        error(parser, "Too many local variables in function.");
        return;
    }

    Local *local = &compiler->locals[compiler->localCount++];
    local->name = name;
    local->depth = -1;
    local->type = type;
}

static void declareVariable(Parser *parser, Token name, Type type) {
    Compiler *compiler = parser->currentCompiler;
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local *local = &compiler->locals[i];
        if (local->depth != -1 && local->depth < compiler->scopeDepth) break;
        if (identifiersEqual(&name, &local->name)) {
            error(parser, "Already a variable with this name in this scope.");
        }
    }
    addLocal(parser, name, type);
}

static void markInitialized(Parser *parser) {
    Compiler *compiler = parser->currentCompiler;
    compiler->locals[compiler->localCount - 1].depth = compiler->scopeDepth;
}

static int resolveLocal(Parser *parser, Token *name) {
    Compiler *compiler = parser->currentCompiler;
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

static Type binary(Parser *parser, bool canAssign) {
    TokenType operatorType = parser->previous.type;
    Type leftType = parser->prevType;
    ParseRule* rule = getRule(operatorType);
    Type rightType = parsePrescedence(parser, (Prescedence)(rule->prescedence + 1));

    Type numberT = type(parser->vm, "Number");
    Type stringT = type(parser->vm, "String");
    Type boolT = type(parser->vm, "Boolean");

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

static Type literal(Parser *parser, bool canAssign) {
    switch (parser->previous.type) {
        case TOKEN_FALSE: emitByte(parser, OP_FALSE); return type(parser->vm, "Boolean");
        case TOKEN_NIL: emitByte(parser, OP_NIL); return NIL_TYPE;
        case TOKEN_TRUE: emitByte(parser, OP_TRUE); return type(parser->vm, "Boolean");
        default: return errorType(parser->vm);
    }
}

static Type grouping(Parser *parser, bool canAssign) {
    Type type = expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
    return type;
}

static Type number(Parser *parser, bool canAssign) {
    double value = strtod(parser->previous.start, NULL);
    emitConstant(parser, NUMBER_VAL(value));
    return type(parser->vm, "Number");
}

static Type string(Parser *parser, bool canAssign) {
    emitConstant(parser, OBJ_VAL(copyString(parser->vm, parser->previous.start + 1, parser->previous.length - 2)));
    return type(parser->vm, "String");
}

static Type parseType(Parser *parser) {
    consume(parser, TOKEN_IDENTIFIER, "Expect type name.");
    Type type = tokenType(parser->vm, parser->previous);
    while (match(parser, TOKEN_PIPE)) {
        consume(parser, TOKEN_IDENTIFIER, "Expect type name after pipe.");
        type = unionType(parser->vm, type, tokenType(parser->vm, parser->previous));
    }
    return type;
}

static Type namedVariable(Parser *parser, Token name, bool canAssign) {
    Compiler *compiler = parser->currentCompiler;

    if (match(parser, TOKEN_COLON)) {
        Type declaredType = parseType(parser);
        consume(parser, TOKEN_EQUAL, "Expect value for variable after type annotation.");
        // Declare before the initializer so a self-reference resolves to this
        // local with depth=-1 and is rejected by resolveLocal.
        declareVariable(parser, name, declaredType);
        Type valueType = expression(parser);
        if (!isSubtype(valueType, declaredType)) {
            typeMismatch(parser, declaredType, valueType, "variable declaration");
        }
        markInitialized(parser);
        uint8_t slot = (uint8_t)(compiler->localCount - 1);
        emitBytes(parser, OP_GET_LOCAL, slot);
        return declaredType;
    }

    int arg = resolveLocal(parser, &name);

    if (canAssign && match(parser, TOKEN_EQUAL)) {
        if (arg == -1) {
            // Implicit declaration: type is inferred from the initializer, so
            // we declare with a placeholder, evaluate, then patch the slot's
            // type. Declaring first still gives us the self-init check. A
            // local with the same name as a native simply shadows it.
            declareVariable(parser, name, errorType(parser->vm));
            Type valueType = expression(parser);
            compiler->locals[compiler->localCount - 1].type = valueType;
            markInitialized(parser);
            uint8_t slot = (uint8_t)(compiler->localCount - 1);
            emitBytes(parser, OP_GET_LOCAL, slot);
            return valueType;
        }
        Type valueType = expression(parser);
        Type existingType = compiler->locals[arg].type;
        if (!isSubtype(valueType, existingType)) {
            typeMismatch(parser, existingType, valueType, "assignment");
        }
        emitBytes(parser, OP_SET_LOCAL, (uint8_t)arg);
        return existingType;
    }

    if (arg != -1) {
        emitBytes(parser, OP_GET_LOCAL, (uint8_t)arg);
        return compiler->locals[arg].type;
    }

    int nativeIdx = resolveNative(parser, &name);
    if (nativeIdx != -1) {
        emitBytes(parser, OP_GET_NATIVE, (uint8_t)nativeIdx);
        return parser->vm->nativeTypes[nativeIdx];
    }

    error(parser, "Undefined variable.");
    return errorType(parser->vm);
}

static Type variable(Parser *parser, bool canAssign) {
    return namedVariable(parser, parser->previous, canAssign);
}

static Type function(Parser *parser, FunctionType funType) {
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

            Type paramType = NIL_TYPE;
            if (match(parser, TOKEN_COLON)) {
                paramType = parseType(parser);
            } else {
                paramType = type(parser->vm, "Any");
            }

            declareVariable(parser, paramName, paramType);
            markInitialized(parser);

            appendTypeArray(&compiler.function->paramaters, paramType);
        } while (match(parser, TOKEN_COMMA));
    }
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

    if (match(parser, TOKEN_GREATER)) {
        compiler.function->returnType = parseType(parser);
    } else {
        compiler.function->returnType = type(parser->vm, "Any");
    }

    Type funcType = functionType(parser->vm, compiler.function->returnType,
                                 &compiler.function->paramaters);
    compiler.locals[0].type = funcType;
    if (compiler.enclosing != NULL && compiler.enclosing->localCount > 0) {
        compiler.enclosing->locals[compiler.enclosing->localCount - 1].type = funcType;
    }

    while (!check(parser, TOKEN_END) && !parser->hadError) {
        expression(parser);
        if (!check(parser, TOKEN_END)) emitByte(parser, OP_POP);
    }
    emitByte(parser, OP_RETURN);

    consume(parser, TOKEN_END, "Expect 'end' after function body.");

    ObjPrototype *functionObj = endCompiler(parser);
    emitBytes(parser, OP_CLOSURE, makeConstant(parser, OBJ_VAL(functionObj)));
    return funcType;
}


// Coerce the value on top of the stack to a real Boolean. `OP_NOT` pushes
// `BOOL_VAL(isFalsey(pop))`, so applying it twice yields `BOOL_VAL(!isFalsey(x))`
// — the truthiness of x as a Boolean. Needed so the static `Boolean` result
// type isn't a lie when short-circuit leaves an operand of any type on the stack.
static void emitCoerceBool(Parser *parser) {
    emitBytes(parser, OP_NOT, OP_NOT);
}

static Type and_(Parser *parser, bool canAssign) {
    emitCoerceBool(parser);                              // coerce left
    int endJump = emitJump(parser, OP_JUMP_IF_FALSE);

    emitByte(parser, OP_POP);
    parsePrescedence(parser, PREC_AND);
    emitCoerceBool(parser);                              // coerce right

    patchJump(parser, endJump);
    return type(parser->vm, "Boolean");
}

static Type or_(Parser *parser, bool canAssign) {
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

static Type unary(Parser *parser, bool canAssign) {
    TokenType operatorType = parser->previous.type;

    Type operandType = parsePrescedence(parser, PREC_UNARY);

    switch (operatorType) {
        case TOKEN_NOT: emitByte(parser, OP_NOT); return type(parser->vm, "Boolean");
        case TOKEN_MINUS:
            expectNumber(parser, operandType, "operand of unary '-'");
            emitByte(parser, OP_NEGATE);
            return type(parser->vm, "Number");
        default: return errorType(parser->vm);
    }
}

static Type retExpr(Parser *parser, bool canAssign) {
    if (parser->currentCompiler->type == TYPE_SCRIPT) {
        error(parser, "Can't return from top-level code.");
        return errorType(parser->vm);
    }

    Type valueType = expression(parser);
    Type expected = parser->currentCompiler->function->returnType;
    if (!isSubtype(valueType, expected)) {
        typeMismatch(parser, expected, valueType, "return value");
    }
    emitByte(parser, OP_RETURN);
    return valueType;
}

static Type call(Parser *parser, bool canAssign) {
    // Snapshot callee type before argumentList — expression() inside the loop
    // will clobber parser->prevType with each argument's type.
    Type calleeType = parser->prevType;

    Type *retSlot = NULL;
    Type *firstParamSlot = NULL;
    bool fnTyped = isFunctionType(calleeType);
    if (fnTyped && calleeType.generics != NULL) {
        retSlot = calleeType.generics;
        firstParamSlot = retSlot->next;
    } else if (!isErrorType(parser, calleeType)) {
        char buf[128], msg[256];
        formatType(calleeType, buf, sizeof(buf));
        snprintf(msg, sizeof(msg), "expected function, got %s", buf);
        typeError(parser, msg);
    }

    uint8_t argCount = 0;
    Type *paramSlot = firstParamSlot;
    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            Type argType = expression(parser);
            if (fnTyped && paramSlot != NULL && paramSlot->generics != NULL) {
                if (!isSubtype(argType, *paramSlot->generics)) {
                    typeMismatch(parser, *paramSlot->generics, argType, "function argument");
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

    if (retSlot != NULL && retSlot->generics != NULL) return *retSlot->generics;
    return errorType(parser->vm);
}

static Type ifExpr(Parser *parser, bool canAssign) {
    expression(parser);

    int thenJump = emitJump(parser, OP_JUMP_IF_FALSE);
    emitByte(parser, OP_POP);
    beginScope(parser);
    Type thenType = NIL_TYPE;
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

    Type elseType = NIL_TYPE;
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

static Type whileExpr(Parser *parser, bool canAssign) {
    // Initial value on stack for first iteration
    emitByte(parser, OP_NIL);

    int loopStart = currentChunk(parser)->code.count;
    expression(parser);

    int exitJump = emitJump(parser, OP_JUMP_IF_FALSE);
    emitByte(parser, OP_POP);  // pop condition
    emitByte(parser, OP_POP);  // pop previous iteration result

    beginScope(parser);
    Type lastType = NIL_TYPE;
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

static Type funExpr(Parser *parser, bool canAssign) {
    consume(parser, TOKEN_IDENTIFIER, "Expect function name.");
    Compiler *outer = parser->currentCompiler;

    // Locals live on the operand stack; the slot is *whatever position the
    // next pushed value lands at*. So declare first (reserving the slot at the
    // current stack top), then let function() push the prototype into it.
    // function() itself patches our outer-local type before compiling the body
    // so recursive references resolve to a proper Function type.
    declareVariable(parser, parser->previous, errorType(parser->vm));
    uint8_t slot = (uint8_t)(outer->localCount - 1);

    Type funcType = function(parser, TYPE_FUNCTION);
    outer->locals[slot].type = funcType;
    markInitialized(parser);

    // The expression-statement loop emits OP_PRINT (or OP_POP) after us, which
    // would consume the function value and leave the slot pointing at stale
    // memory. Duplicate via OP_GET_LOCAL so the consumer eats the copy.
    emitBytes(parser, OP_GET_LOCAL, slot);
    return funcType;
}


ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, call,   PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
    [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
    [TOKEN_COLON]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
    [TOKEN_AND]           = {and_,     NULL,   PREC_NONE},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_END]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DEF]           = {funExpr,  NULL,   PREC_NONE},
    [TOKEN_IF]            = {ifExpr,   NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
    [TOKEN_NOT]           = {unary,    NULL,   PREC_NONE},
    [TOKEN_OR]            = {or_,      NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {retExpr,  NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SELF]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {whileExpr,NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};


static Type parsePrescedence(Parser *parser, Prescedence prescedence) {
    advance(parser);
    ParseFn prefixRule = getRule(parser->previous.type)->prefix;
    Type exprType;
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

static Type expression(Parser *parser) {
    return parsePrescedence(parser, PREC_ASSIGNMENT);
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
    Compiler compiler;
    initCompiler(&compiler, &parser, TYPE_SCRIPT);
    parser.currentCompiler = &compiler;
    
    advance(&parser);
    
    while (!match(&parser, TOKEN_EOF) && !parser.hadError) {
        expression(&parser);
    }
    
    ObjPrototype *function = endCompiler(&parser);

    #ifdef SLC_DEBUG
    if (!parser.hadError) {
        // endCompiler popped the script compiler off, so currentChunk(&parser)
        // would dereference a NULL currentCompiler. Use the returned function's
        // chunk directly.
        disassembleChunk(&function->chunk, function->name != NULL ? function->name->chars : "<script>");
    }
    #endif

    return parser.hadError ? NULL : function;
}
