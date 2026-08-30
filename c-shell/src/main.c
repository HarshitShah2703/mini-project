#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "builtins.h"
#include "executor.h"
#include "lexer.h"
#include "parser.h"
#include "prompt.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    char home_directory[PATH_MAX];
    prompt_init(home_directory, sizeof(home_directory));

    ShellState state;
    shell_state_init(&state, home_directory);

    char *line = NULL;
    size_t line_capacity = 0;

    while (1) {
        prompt_display(home_directory);

        ssize_t line_length = getline(&line, &line_capacity, stdin);
        if (line_length == -1) {
            putchar('\n');
            break;
        }

        TokenList tokens;
        if (lexer_tokenize(line, &tokens) != 0) {
            printf("cshell: invalid syntax\n");
            continue;
        }

        if (!parser_validate(&tokens)) {
            printf("cshell: invalid syntax\n");
            lexer_free(&tokens);
            continue;
        }

        Pipeline pipeline;
        build_pipeline(&tokens, &pipeline);
        lexer_free(&tokens);

        execute_pipeline(&pipeline, &state);
        free_pipeline(&pipeline);
    }

    free(line);
    return EXIT_SUCCESS;
}

