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

typedef void (*ParseFn)(Parser*);

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


static void emitByte(Parser *parser, uint8_t byte) {
    writeChunk(currentChunk(parser), byte, parser->previous.line);
}

static void emitBytes(Parser *parser, uint8_t byte1, uint8_t byte2) {
    emitByte(parser, byte1);
    emitByte(parser, byte2);
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


static void endCompiler(Parser *parser) {
    emitByte(parser, OP_RETURN);
}


static void expression(Parser *parser);
static ParseRule *getRule(TokenType type);
static void parsePrescedence(Parser *parser, Prescedence prescedence);


static void binary(Parser *parser) {
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

static void literal(Parser *parser) {
    switch (parser->previous.type) {
        case TOKEN_FALSE: emitByte(parser, OP_FALSE); break;
        case TOKEN_NIL: emitByte(parser, OP_NIL); break;
        case TOKEN_TRUE: emitByte(parser, OP_TRUE); break;
        default: return;
    }
}

static void grouping(Parser *parser) {
    expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(Parser *parser) {
    double value = strtod(parser->previous.start, NULL);
    emitConstant(parser, NUMBER_VAL(value));
}

static void unary(Parser *parser) {
    TokenType operatorType = parser->previous.type;

    parsePrescedence(parser, PREC_UNARY);

    switch (operatorType) {
        case TOKEN_NOT: emitByte(parser, OP_NOT); break;
        case TOKEN_MINUS: emitByte(parser, OP_NEGATE); break;
        default: return;
    }
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
    [TOKEN_GREATER]       = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_LESS]          = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_IDENTIFIER]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_STRING]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
    [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_END]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DEF]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
    [TOKEN_NOT]           = {unary,    NULL,   PREC_NONE},
    [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SELF]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
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

    prefixRule(parser);

    while (prescedence <= getRule(parser->current.type)->prescedence) {
        advance(parser);
        ParseFn infixRule = getRule(parser->previous.type)->infix;
        infixRule(parser);
    }
}

static ParseRule *getRule(TokenType type) {
    return &rules[type];
}

static void expression(Parser *parser) {
    parsePrescedence(parser, PREC_ASSIGNMENT);
}


bool compile(const char *source, Chunk *chunk) {
    Lexer lexer;
    initLexer(&lexer, source);
    Parser parser;
    parser.lexer = &lexer;
    parser.hadError = false;
    parser.panicMode = false;
    parser.compilingChunk = chunk;
    
    advance(&parser);
    expression(&parser);
    consume(&parser, TOKEN_EOF, "Expect end of expression.");
    
    endCompiler(&parser);

    #ifdef SLC_DEBUG
    if (!parser.hadError) {
        disassembleChunk(currentChunk(&parser), "code");
    }
    #endif

    return !parser.hadError;
}
