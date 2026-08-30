#define _POSIX_C_SOURCE 200809L

#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static int token_list_push(
    TokenList *tokens,
    TokenType type,
    const char *value
) {
    if (tokens->count == tokens->capacity) {
        size_t new_capacity =
            tokens->capacity == 0 ? 16 : tokens->capacity * 2;

        Token *new_items = realloc(
            tokens->items,
            new_capacity * sizeof(Token)
        );

        if (new_items == NULL) {
            return -1;
        }

        tokens->items = new_items;
        tokens->capacity = new_capacity;
    }

    tokens->items[tokens->count].type = type;

    tokens->items[tokens->count].value = strdup(value);
    if (tokens->items[tokens->count].value == NULL) {
        return -1;
    }

    tokens->count++;
    return 0;
}

static int word_append(
    char **word,
    size_t *length,
    size_t *capacity,
    char character
) {
    if (*length + 1 >= *capacity) {
        size_t new_capacity =
            *capacity == 0 ? 16 : *capacity * 2;

        char *new_word = realloc(*word, new_capacity);
        if (new_word == NULL) {
            return -1;
        }

        *word = new_word;
        *capacity = new_capacity;
    }

    (*word)[*length] = character;
    (*length)++;
    (*word)[*length] = '\0';

    return 0;
}

static int is_special(char character) {
    return character == '|' ||
           character == '&' ||
           character == ';' ||
           character == '<' ||
           character == '>';
}

static int lex_word(
    const char *line,
    size_t *position,
    char **result
) {
    char *word = NULL;
    size_t length = 0;
    size_t capacity = 0;

    while (line[*position] != '\0') {
        char character = line[*position];

        if (character == ' ' ||
            character == '\t' ||
            character == '\n' ||
            character == '\r' ||
            is_special(character)) {
            break;
        }

        if (character == '\\') {
            (*position)++;

            if (line[*position] == '\0' ||
                line[*position] == '\n') {
                free(word);
                return -1;
            }

            if (word_append(
                    &word,
                    &length,
                    &capacity,
                    line[*position]
                ) != 0) {
                free(word);
                return -1;
            }

            (*position)++;
            continue;
        }

        if (character == '\'') {
            (*position)++;

            while (line[*position] != '\0' &&
                   line[*position] != '\'') {
                if (word_append(
                        &word,
                        &length,
                        &capacity,
                        line[*position]
                    ) != 0) {
                    free(word);
                    return -1;
                }

                (*position)++;
            }

            if (line[*position] != '\'') {
                free(word);
                return -1;
            }

            (*position)++;
            continue;
        }

        if (character == '"') {
            (*position)++;

            while (line[*position] != '\0' &&
                   line[*position] != '"') {
                character = line[*position];

                if (character == '\\') {
                    (*position)++;

                    if (line[*position] == '\0' ||
                        line[*position] == '\n') {
                        free(word);
                        return -1;
                    }

                    character = line[*position];

                    if (character != '"' &&
                        character != '\\') {
                        if (word_append(
                                &word,
                                &length,
                                &capacity,
                                '\\'
                            ) != 0) {
                            free(word);
                            return -1;
                        }
                    }

                    if (word_append(
                            &word,
                            &length,
                            &capacity,
                            character
                        ) != 0) {
                        free(word);
                        return -1;
                    }

                    (*position)++;
                    continue;
                }

                if (word_append(
                        &word,
                        &length,
                        &capacity,
                        character
                    ) != 0) {
                    free(word);
                    return -1;
                }

                (*position)++;
            }

            if (line[*position] != '"') {
                free(word);
                return -1;
            }

            (*position)++;
            continue;
        }

        if (word_append(
                &word,
                &length,
                &capacity,
                character
            ) != 0) {
            free(word);
            return -1;
        }

        (*position)++;
    }

    if (word == NULL) {
        word = strdup("");
        if (word == NULL) {
            return -1;
        }
    }

    *result = word;
    return 0;
}

int lexer_tokenize(const char *line, TokenList *tokens) {
    tokens->items = NULL;
    tokens->count = 0;
    tokens->capacity = 0;

    size_t position = 0;

    while (line[position] != '\0') {
        char character = line[position];

        if (character == ' ' ||
            character == '\t' ||
            character == '\n' ||
            character == '\r') {
            position++;
            continue;
        }

        if (character == '|') {
            if (token_list_push(tokens, TOKEN_PIPE, "|") != 0) {
                lexer_free(tokens);
                return -1;
            }

            position++;
            continue;
        }

        if (character == '&') {
            if (token_list_push(tokens, TOKEN_AMP, "&") != 0) {
                lexer_free(tokens);
                return -1;
            }

            position++;
            continue;
        }

        if (character == ';') {
            if (token_list_push(tokens, TOKEN_SEMI, ";") != 0) {
                lexer_free(tokens);
                return -1;
            }

            position++;
            continue;
        }

        if (character == '<') {
            if (token_list_push(tokens, TOKEN_LT, "<") != 0) {
                lexer_free(tokens);
                return -1;
            }

            position++;
            continue;
        }

        if (character == '>') {
            if (line[position + 1] == '>') {
                if (token_list_push(tokens, TOKEN_GTGT, ">>") != 0) {
                    lexer_free(tokens);
                    return -1;
                }

                position += 2;
            } else {
                if (token_list_push(tokens, TOKEN_GT, ">") != 0) {
                    lexer_free(tokens);
                    return -1;
                }

                position++;
            }

            continue;
        }

        char *word = NULL;

        if (lex_word(line, &position, &word) != 0) {
            lexer_free(tokens);
            return -1;
        }

        if (token_list_push(tokens, TOKEN_WORD, word) != 0) {
            free(word);
            lexer_free(tokens);
            return -1;
        }

        free(word);
    }

    return 0;
}

void lexer_free(TokenList *tokens) {
    for (size_t index = 0; index < tokens->count; index++) {
        free(tokens->items[index].value);
    }

    free(tokens->items);

    tokens->items = NULL;
    tokens->count = 0;
    tokens->capacity = 0;
}

const char *token_type_name(TokenType type) {
    switch (type) {
        case TOKEN_WORD:
            return "WORD";
        case TOKEN_PIPE:
            return "|";
        case TOKEN_AMP:
            return "&";
        case TOKEN_SEMI:
            return ";";
        case TOKEN_LT:
            return "<";
        case TOKEN_GT:
            return ">";
        case TOKEN_GTGT:
            return ">>";
    }

    return "UNKNOWN";
}
