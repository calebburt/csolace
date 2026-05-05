#include "common.h"
#include "compiler.h"
#include "lexer.h"
#include "type.h"
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
    PREC_PRIMARY
} Prescedence;

typedef struct {
    Token name;
    Type type;
    int depth;
} Local;

typedef struct {
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
    return parser->compilingChunk;
}


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

static void initCompiler(Compiler *compiler) {
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
}

static void patchJump(Parser *parser, int offset) {
    int jump = currentChunk(parser)->code.count - offset - 2;

    if (jump > UINT16_MAX) {
        error(parser, "Too much code to jump over.");
    }

    currentChunk(parser)->code.data[offset] = (jump >> 8) & 0xff;
    currentChunk(parser)->code.data[offset + 1] = jump & 0xff;
}


static void endCompiler(Parser *parser) {
    emitByte(parser, OP_RETURN);
}

static void beginScope(Parser *parser) {
    parser->currentCompiler->scopeDepth++;
}

static void endScope(Parser *parser) {
    parser->currentCompiler->scopeDepth--;
}


static Type expression(Parser *parser);
static ParseRule *getRule(TokenType type);
static Type parsePrescedence(Parser *parser, Prescedence prescedence);

static bool isErrorType(Parser *parser, Type t) {
    return typesEqual(t, errorType(parser->vm));
}

static const char *typeName(Type t) {
    return t.name == NULL ? "<unknown>" : t.name->chars;
}

static void typeMismatch(Parser *parser, Type expected, Type actual, const char *context) {
    if (isErrorType(parser, expected) || isErrorType(parser, actual)) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "expected %s in %s, got %s",
             typeName(expected), context, typeName(actual));
    typeError(parser, buf);
}

static void expectNumber(Parser *parser, Type actual, const char *context) {
    if (isErrorType(parser, actual)) return;
    Type numberT = type(parser->vm, "Number");
    if (!typesEqual(actual, numberT)) {
        typeMismatch(parser, numberT, actual, context);
    }
}


static int addLocal(Parser *parser, Token name, Type type) {
    if (parser->currentCompiler->localCount == UINT8_COUNT) {
        error(parser, "Too many local variables in function.");
        return -1;
    }

    int slot = parser->currentCompiler->localCount++;
    Local *local = &parser->currentCompiler->locals[slot];
    local->name = name;
    local->depth = parser->currentCompiler->scopeDepth;
    local->type = type;
    return slot;
}

static int resolveLocal(Parser *parser, Token *name) {
    Compiler *compiler = parser->currentCompiler;
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local *local = &compiler->locals[i];
        if (name->length == local->name.length &&
            memcmp(name->start, local->name.start, name->length) == 0) {
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
        case TOKEN_NIL: emitByte(parser, OP_NIL); return type(parser->vm, "Nil");
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
    consume(parser, TOKEN_IDENTIFIER, "Expect type name."); // will amend later
    return tokenType(parser->vm, parser->previous);
}

static Type namedVariable(Parser *parser, Token name, bool canAssign) {
    if (match(parser, TOKEN_COLON)) {
        Type declaredType = parseType(parser);
        consume(parser, TOKEN_EQUAL, "Expect value for variable after type annotation.");
        Type valueType = expression(parser);
        if (!typesEqual(valueType, declaredType)) {
            typeMismatch(parser, declaredType, valueType, "variable declaration");
        }
        int slot = addLocal(parser, name, declaredType);
        if (slot < 0) return errorType(parser->vm);
        emitBytes(parser, OP_GET_LOCAL, (uint8_t)slot);
        return declaredType;
    }

    int arg = resolveLocal(parser, &name);

    if (canAssign && match(parser, TOKEN_EQUAL)) {
        Type valueType = expression(parser);
        if (arg == -1) {
            int slot = addLocal(parser, name, valueType);
            if (slot < 0) return errorType(parser->vm);
            emitBytes(parser, OP_GET_LOCAL, (uint8_t)slot);
            return valueType;
        }
        Type existingType = parser->currentCompiler->locals[arg].type;
        if (!typesEqual(valueType, existingType)) {
            typeMismatch(parser, existingType, valueType, "assignment");
        }
        emitBytes(parser, OP_SET_LOCAL, (uint8_t)arg);
        return existingType;
    }

    if (arg == -1) {
        error(parser, "Undefined variable.");
        return errorType(parser->vm);
    }
    emitBytes(parser, OP_GET_LOCAL, (uint8_t)arg);
    return parser->currentCompiler->locals[arg].type;
}

static Type variable(Parser *parser, bool canAssign) {
    return namedVariable(parser, parser->previous, canAssign);
}

static Type and_(Parser *parser, bool canAssign) {
    int endJump = emitJump(parser, OP_JUMP_IF_FALSE);

    emitByte(parser, OP_POP);
    parsePrescedence(parser, PREC_AND);

    patchJump(parser, endJump);
    return type(parser->vm, "Boolean");
}

static Type or_(Parser *parser, bool canAssign) {
    int elseJump = emitJump(parser, OP_JUMP_IF_FALSE);
    int endJump = emitJump(parser, OP_JUMP);

    patchJump(parser, elseJump);
    emitByte(parser, OP_POP);


    parsePrescedence(parser, PREC_OR);
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

static Type ifExpr(Parser *parser, bool canAssign) {
    expression(parser);

    int thenJump = emitJump(parser, OP_JUMP_IF_FALSE);
    emitByte(parser, OP_POP);
    beginScope(parser);
    Type thenType;
    while (!check(parser, TOKEN_END) && !check(parser, TOKEN_ELSE) && !parser->hadError) {
        thenType = expression(parser);
        if (!check(parser, TOKEN_END) && !check(parser, TOKEN_ELSE)) emitByte(parser, OP_POP);
    }
    endScope(parser);

    int elseJump = emitJump(parser, OP_JUMP);

    patchJump(parser, thenJump);
    emitByte(parser, OP_POP);

    Type elseType;
    if (match(parser, TOKEN_ELSE)) {
        beginScope(parser);
        while (!check(parser, TOKEN_END) && !parser->hadError) {
            elseType = expression(parser);
            if (!check(parser, TOKEN_END)) emitByte(parser, OP_POP);
        }
        endScope(parser);
        consume(parser, TOKEN_END, "Expected 'end' after else block.");
    } else {
        consume(parser, TOKEN_END, "Expected 'end' after if block.");
        emitByte(parser, OP_NIL);
        elseType = type(parser->vm, "Nil");
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
    Type lastType;
    while (!check(parser, TOKEN_END) && !parser->hadError) {
        lastType = expression(parser);
        if (!check(parser, TOKEN_END)) emitByte(parser, OP_POP);
    }
    endScope(parser);
    consume(parser, TOKEN_END, "Expected 'end' after while body.");

    // At this point, body result is on stack
    // Loop back to test condition again with body result still there
    emitLoop(parser, loopStart);

    patchJump(parser, exitJump);
    emitByte(parser, OP_POP);  // pop the false condition
    return lastType;
}


ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
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
    [TOKEN_DEF]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {ifExpr,   NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
    [TOKEN_NOT]           = {unary,    NULL,   PREC_NONE},
    [TOKEN_OR]            = {or_,      NULL,   PREC_NONE},
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
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


bool compile(VM *vm, const char *source, Chunk *chunk) {
    Lexer lexer;
    initLexer(&lexer, source);
    Parser parser;
    parser.lexer = &lexer;
    parser.hadError = false;
    parser.panicMode = false;
    parser.compilingChunk = chunk;
    parser.vm = vm;
    parser.prevType = errorType(vm);
    Compiler compiler;
    initCompiler(&compiler);
    parser.currentCompiler = &compiler;
    
    advance(&parser);
    
    while (!match(&parser, TOKEN_EOF) && !parser.hadError) {
        expression(&parser);
        emitByte(&parser, OP_PRINT);
    }
    
    endCompiler(&parser);

    #ifdef SLC_DEBUG
    if (!parser.hadError) {
        disassembleChunk(currentChunk(&parser), "code");
    }
    #endif

    return !parser.hadError;
}
