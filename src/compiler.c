#include "common.h"
#include "compiler.h"
#include "lexer.h"
#ifdef SLC_DEBUG
#include "debug.h"
#endif

typedef struct {
    Token current;
    Token previous;
    Lexer *lexer;
    Chunk *compilingChunk;
    VM *vm;
    bool hadError;
    bool panicMode;
} Parser;

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

typedef void (*ParseFn)(Parser* parser, bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Prescedence prescedence;
} ParseRule;


static Chunk *currentChunk(Parser *parser) {
    return parser->compilingChunk;
}


static void errorAt(Parser *parser, Token *token, const char *message) {
    if (parser->panicMode) return;
    parser->panicMode = true;
    if (token->type != TOKEN_ERROR)
        fprintf(stderr, "\033[31mSyntaxError: %s \033[0m\n", message);
    else
        fprintf(stderr, "\033[31mSyntaxError: %s \033[0m\n", token->start);
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, "\033[33m at end\033[0m");
    } else if (token->type == TOKEN_ERROR) {

    } else {
        fprintf(stderr, "\033[33m at line %d\033[0m", token->line);
    }
    fprintf(stderr, "\n");

    parser->hadError = true;
}

static void errorAtCurrent(Parser *parser, const char *message) {
    errorAt(parser, &parser->current, message);
}

static void error(Parser *parser, const char *message) {
    errorAt(parser, &parser->previous, message);
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


static void expression(Parser *parser);
static ParseRule *getRule(TokenType type);
static void parsePrescedence(Parser *parser, Prescedence prescedence);


static uint8_t identifierConstant(Parser *parser, Token *name) {
    return makeConstant(parser, OBJ_VAL(copyString(parser->vm, name->start, name->length)));
}

static void binary(Parser *parser, bool canAssign) {
    TokenType operatorType = parser->previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrescedence(parser, (Prescedence)(rule->prescedence + 1));

    switch (operatorType) {
        case TOKEN_BANG_EQUAL: emitBytes(parser, OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL: emitByte(parser, OP_EQUAL); break;
        case TOKEN_GREATER: emitByte(parser, OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitByte(parser, OP_GREATER_EQUAL); break;
        case TOKEN_LESS: emitByte(parser, OP_LESS); break;
        case TOKEN_LESS_EQUAL: emitByte(parser, OP_LESS_EQUAL); break;
        case TOKEN_PLUS: emitByte(parser, OP_ADD); break;
        case TOKEN_MINUS: emitByte(parser, OP_SUBTRACT); break;
        case TOKEN_STAR: emitByte(parser, OP_MULTIPLY); break;
        case TOKEN_SLASH: emitByte(parser, OP_DIVIDE); break;
        default: return; // Unreachable.
    }
}

static void literal(Parser *parser, bool canAssign) {
    switch (parser->previous.type) {
        case TOKEN_FALSE: emitByte(parser, OP_FALSE); break;
        case TOKEN_NIL: emitByte(parser, OP_NIL); break;
        case TOKEN_TRUE: emitByte(parser, OP_TRUE); break;
        default: return;
    }
}

static void grouping(Parser *parser, bool canAssign) {
    expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(Parser *parser, bool canAssign) {
    double value = strtod(parser->previous.start, NULL);
    emitConstant(parser, NUMBER_VAL(value));
}

static void string(Parser *parser, bool canAssign) {
    emitConstant(parser, OBJ_VAL(copyString(parser->vm, parser->previous.start + 1, parser->previous.length - 2)));
}

static void parseType(Parser *parser) {
    consume(parser, TOKEN_IDENTIFIER, "Expect type name."); // will amend later
}

static void namedVariable(Parser *parser, Token name, bool canAssign) {
    uint8_t arg = identifierConstant(parser, &name);
    
    if (match(parser, TOKEN_COLON)) {
        parseType(parser);
        consume(parser, TOKEN_EQUAL, "Expect value for variable after type annotation.");
        expression(parser);
        emitBytes(parser, OP_SET_GLOBAL, arg);
        return;
    } else {
        if (!check(parser, TOKEN_EQUAL)) {
            emitBytes(parser, OP_GET_GLOBAL, arg);
            return;
        }
    }
    if (canAssign && match(parser, TOKEN_EQUAL)) {
        expression(parser);
        emitBytes(parser, OP_SET_GLOBAL, arg);
    }
}

static void variable(Parser *parser, bool canAssign) {
    namedVariable(parser, parser->previous, canAssign);
}

static void and_(Parser *parser, bool canAssign) {
    int endJump = emitJump(parser, OP_JUMP_IF_FALSE);

    emitByte(parser, OP_POP);
    parsePrescedence(parser, PREC_AND);

    patchJump(parser, endJump);
}

static void or_(Parser *parser, bool canAssign) {
    int elseJump = emitJump(parser, OP_JUMP_IF_FALSE);
    int endJump = emitJump(parser, OP_JUMP);

    patchJump(parser, elseJump);
    emitByte(parser, OP_POP);


    parsePrescedence(parser, PREC_OR);
    patchJump(parser, endJump);
}

static void unary(Parser *parser, bool canAssign) {
    TokenType operatorType = parser->previous.type;

    parsePrescedence(parser, PREC_UNARY);

    switch (operatorType) {
        case TOKEN_NOT: emitByte(parser, OP_NOT); break;
        case TOKEN_MINUS: emitByte(parser, OP_NEGATE); break;
        default: return;
    }
}

static void ifExpr(Parser *parser, bool canAssign) {
    expression(parser);

    int thenJump = emitJump(parser, OP_JUMP_IF_FALSE);
    emitByte(parser, OP_POP);
    while (!check(parser, TOKEN_END) && !check(parser, TOKEN_ELSE) && !parser->hadError) {
        expression(parser);
        if (!check(parser, TOKEN_END) && !check(parser, TOKEN_ELSE)) emitByte(parser, OP_POP);
    }

    int elseJump = emitJump(parser, OP_JUMP);

    patchJump(parser, thenJump);
    emitByte(parser, OP_POP);

    if (match(parser, TOKEN_ELSE)) {
        while (!check(parser, TOKEN_END) && !parser->hadError) {
            expression(parser);
            if (!check(parser, TOKEN_END)) emitByte(parser, OP_POP);
        }
        consume(parser, TOKEN_END, "Expected 'end' after else block.");
    } else {
        consume(parser, TOKEN_END, "Expected 'end' after if block.");
        emitByte(parser, OP_NIL);
    }
    patchJump(parser, elseJump);
}

static void whileExpr(Parser *parser, bool canAssign) {
    // Initial value on stack for first iteration
    emitByte(parser, OP_NIL);
    
    int loopStart = currentChunk(parser)->code.count;
    expression(parser);

    int exitJump = emitJump(parser, OP_JUMP_IF_FALSE);
    emitByte(parser, OP_POP);  // pop condition
    emitByte(parser, OP_POP);  // pop previous iteration result
    
    while (!check(parser, TOKEN_END) && !parser->hadError) {
        expression(parser);
        if (!check(parser, TOKEN_END)) emitByte(parser, OP_POP);
    }
    consume(parser, TOKEN_END, "Expected 'end' after while body.");

    // At this point, body result is on stack
    // Loop back to test condition again with body result still there
    emitLoop(parser, loopStart);

    patchJump(parser, exitJump);
    emitByte(parser, OP_POP);  // pop the false condition
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


static void parsePrescedence(Parser *parser, Prescedence prescedence) {
    advance(parser);
    ParseFn prefixRule = getRule(parser->previous.type)->prefix;
    if (prefixRule == NULL) {
        error(parser, "Expect expression.");
        return;
    }

    bool canAssign = prescedence <= PREC_ASSIGNMENT;
    prefixRule(parser, canAssign);

    while (prescedence <= getRule(parser->current.type)->prescedence) {
        advance(parser);
        ParseFn infixRule = getRule(parser->previous.type)->infix;
        infixRule(parser, canAssign);
    }

    if (canAssign && match(parser, TOKEN_EQUAL)) {
        error(parser, "Invalid assignment target.");
    }
}

static ParseRule *getRule(TokenType type) {
    return &rules[type];
}

static void expression(Parser *parser) {
    parsePrescedence(parser, PREC_ASSIGNMENT);
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
