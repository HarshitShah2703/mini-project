#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,
    TOKEN_AMP,
    TOKEN_SEMI,
    TOKEN_LT,
    TOKEN_GT,
    TOKEN_GTGT
} TokenType;

typedef struct {
    TokenType type;
    char *value;
} Token;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

int lexer_tokenize(const char *line, TokenList *tokens);
void lexer_free(TokenList *tokens);
const char *token_type_name(TokenType type);

#endif
