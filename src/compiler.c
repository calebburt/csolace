#include "common.h"
#include "compiler.h"
#include "lexer.h"

void compile(const char *source) {
    Lexer lexer;
    initLexer(&lexer, source);
    int line = -1;
    while (true) {
        Token token = scanToken(&lexer);
        if (token.line != line) {
            printf("%4d ", token.line);
            line = token.line;
        } else {
            printf("   | ");
        }
        printf("%2d '%.*s'\n", token.type, token.length, token.start);

        if (token.type == TOKEN_EOF) break;
    }
}
