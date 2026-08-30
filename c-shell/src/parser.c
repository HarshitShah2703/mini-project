#include "parser.h"

static int is_word(const TokenList *tokens, size_t position) {
    return position < tokens->count &&
           tokens->items[position].type == TOKEN_WORD;
}

static int parse_command(
    const TokenList *tokens,
    size_t *position
);

static int parse_arguments(
    const TokenList *tokens,
    size_t *position
) {
    while (*position < tokens->count) {
        TokenType type = tokens->items[*position].type;

        if (type == TOKEN_WORD) {
            (*position)++;
            continue;
        }

        if (type == TOKEN_LT ||
            type == TOKEN_GT ||
            type == TOKEN_GTGT) {
            (*position)++;

            if (!is_word(tokens, *position)) {
                return 0;
            }

            (*position)++;
            continue;
        }

        if (type == TOKEN_PIPE ||
            type == TOKEN_SEMI) {
            (*position)++;

            return parse_command(tokens, position);
        }

        if (type == TOKEN_AMP) {
            (*position)++;

            if (*position == tokens->count) {
                return 1;
            }

            return parse_command(tokens, position);
        }

        return 0;
    }

    return 1;
}

static int parse_command(
    const TokenList *tokens,
    size_t *position
) {
    if (!is_word(tokens, *position)) {
        return 0;
    }

    (*position)++;

    return parse_arguments(tokens, position);
}

int parser_validate(const TokenList *tokens) {
    if (tokens->count == 0) {
        return 1;
    }

    size_t position = 0;

    if (!parse_command(tokens, &position)) {
        return 0;
    }

    return position == tokens->count;
}
