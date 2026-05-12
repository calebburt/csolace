#ifndef SLC_LEXER_H
#define SLC_LEXER_H

typedef struct {
    const char *start;
    const char *current;
    int line;
} Lexer;

typedef enum {
    // single-char tokens
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_SLASH, TOKEN_STAR, TOKEN_COLON, TOKEN_PIPE,
    // 1 or 2 char tokens
    TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    // literals
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,
    // keywords
    TOKEN_AND, TOKEN_CLASS, TOKEN_ELSE, TOKEN_END, TOKEN_FALSE,
    TOKEN_FOR, TOKEN_DEF, TOKEN_IF, TOKEN_NIL, TOKEN_NOT, TOKEN_OR,
    TOKEN_PRINT, TOKEN_RETURN, TOKEN_SUPER, TOKEN_SELF,
    TOKEN_TRUE, TOKEN_WHILE,

    TOKEN_ERROR, TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    const char *start;
    int length;
    int line;
} Token;

void initLexer(Lexer *lexer, const char *source);
Token scanToken(Lexer *lexer);

#endif