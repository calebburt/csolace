#include "common.h"
#include "lexer.h"

void initLexer(Lexer *lexer, const char *source) {
    lexer->start = source;
    lexer->current = source;
    lexer->line = 1;
}

static bool isAtEnd(Lexer *lexer) {
    return *lexer->current == '\0';
}

static char advance(Lexer *lexer) {
    lexer->current++;
    return lexer->current[-1];
}

static bool match(Lexer *lexer, char expected) {
    if (isAtEnd(lexer)) return false;
    if (*lexer->current != expected) return false;
    lexer->current++;
    return true;
}

static char peek(Lexer *lexer) {
    return *lexer->current;
}

static char peekNext(Lexer *lexer) {
    if (isAtEnd(lexer)) return '\0';
    return lexer->current[1];
}

static Token makeToken(Lexer *lexer, TokenType type) {
    Token token;
    token.type = type;
    token.start = lexer->start;
    token.length = (int)(lexer->current - lexer->start);
    token.line = lexer->line;
    return token;
}

static Token errorToken(Lexer *lexer, const char *message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = lexer->line;
    return token;
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static TokenType checkKeyword(Lexer *lexer, int start, int length,
                              const char *rest, TokenType type) {
    if (lexer->current - lexer->start == start + length &&
        memcmp(lexer->start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static TokenType identifierType(Lexer *lexer) {
    switch (lexer->start[0]) {
        case 'a': return checkKeyword(lexer, 1, 2, "nd", TOKEN_AND);
        case 'c': return checkKeyword(lexer, 1, 4, "lass", TOKEN_CLASS);
        case 'd': return checkKeyword(lexer, 1, 2, "ef", TOKEN_DEF);
        case 'e':
            if (lexer->current - lexer->start > 1) {
                switch (lexer->start[1]) {
                    case 'l': return checkKeyword(lexer, 2, 2, "se", TOKEN_ELSE);
                    case 'n': return checkKeyword(lexer, 2, 1, "d", TOKEN_END);
                }
            }
            break;
        case 'f':
            if (lexer->current - lexer->start > 1) {
                switch (lexer->start[1]) {
                    case 'a': return checkKeyword(lexer, 2, 3, "lse", TOKEN_FALSE);
                    case 'o': return checkKeyword(lexer, 2, 1, "r", TOKEN_FOR);
                }
            }
            break;
        case 'i': return checkKeyword(lexer, 1, 1, "f", TOKEN_IF);
        case 'n':
            if (lexer->current - lexer->start > 1) {
                switch (lexer->start[1]) {
                    case 'i': return checkKeyword(lexer, 2, 1, "l", TOKEN_NIL);
                    case 'o': return checkKeyword(lexer, 2, 1, "t", TOKEN_NOT);
                }
            }
            break;
        case 'o':
            if (lexer->current - lexer->start > 1) {
                switch (lexer->start[1]) {
                    case 'r': return TOKEN_OR;
                    case 'u': return checkKeyword(lexer, 2, 3, "ter", TOKEN_OUTER);
                }
            }
            break;
        case 'r': return checkKeyword(lexer, 1, 5, "eturn", TOKEN_RETURN);
        case 's':
            if (lexer->current - lexer->start > 1) {
                switch (lexer->start[1]) {
                    case 'e': return checkKeyword(lexer, 2, 2, "lf", TOKEN_SELF);
                    case 'u': return checkKeyword(lexer, 2, 3, "per", TOKEN_SUPER);
                }
            }
            break;
        case 't': return checkKeyword(lexer, 1, 3, "rue", TOKEN_TRUE);
        case 'w': return checkKeyword(lexer, 1, 4, "hile", TOKEN_WHILE);
    }

    return TOKEN_IDENTIFIER;
}

static void skipWhitespace(Lexer *lexer) {
    while (true) {
        char c = peek(lexer);
        switch (c) {
            case '\n':
                lexer->line++;
                __attribute__((fallthrough));
            case ' ':
            case '\r':
            case '\t':
                advance(lexer);
                break;
            case '#':
                while (peek(lexer) != '\n' && !isAtEnd(lexer)) advance(lexer);
                break;
            default:
                return;
        }
    }
}

static Token string(Lexer *lexer) {
    while (peek(lexer) != '"' && !isAtEnd(lexer)) {
        if (peek(lexer) == '\n') lexer->line++;
        advance(lexer);
    }

    if (isAtEnd(lexer)) return errorToken(lexer, "Unterminated string.");

    advance(lexer);
    return makeToken(lexer, TOKEN_STRING);
}

static Token number(Lexer *lexer) {
    while (isDigit(peek(lexer))) advance(lexer);

    if (peek(lexer) == '.' && isDigit(peekNext(lexer))) {
        advance(lexer);

        while (isDigit(peek(lexer))) advance(lexer);
    }

    return makeToken(lexer, TOKEN_NUMBER);
}

static Token identifier(Lexer *lexer) {
    while (isAlpha(peek(lexer)) || isDigit(peek(lexer))) advance(lexer);
    return makeToken(lexer, identifierType(lexer));
}

Token scanToken(Lexer *lexer) {
    skipWhitespace(lexer);
    lexer->start = lexer->current;

    if (isAtEnd(lexer)) return makeToken(lexer, TOKEN_EOF);

    char c = advance(lexer);

    if (isDigit(c)) return number(lexer);
    if (isAlpha(c)) return identifier(lexer);

    switch (c) {
        case '(': return makeToken(lexer, TOKEN_LEFT_PAREN);
        case ')': return makeToken(lexer, TOKEN_RIGHT_PAREN);
        case ',': return makeToken(lexer, TOKEN_COMMA);
        case '.': return makeToken(lexer, TOKEN_DOT);
        case '-': return makeToken(lexer, TOKEN_MINUS);
        case '+': return makeToken(lexer, TOKEN_PLUS);
        case '/': return makeToken(lexer, TOKEN_SLASH);
        case '*': return makeToken(lexer, TOKEN_STAR);
        case ':': return makeToken(lexer, TOKEN_COLON);
        case '|': return makeToken(lexer, TOKEN_PIPE);
        case '!': {
                if (match(lexer, '=')) return makeToken(lexer, TOKEN_BANG_EQUAL); else break;
            }
        case '=':
            return makeToken(lexer, match(lexer, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<':
            return makeToken(lexer, match(lexer, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return makeToken(lexer, match(lexer, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '"':
            return string(lexer);
    }

    static char buf[32];
    snprintf(buf, sizeof(buf), "Unexpected character '%c'.", c);
    return errorToken(lexer, buf);
}
