#ifndef BUILTINS_H
#define BUILTINS_H

#include <stddef.h>

typedef struct {
    char home_directory[4096];
    char previous_directory[4096];
    int has_previous_directory;
} ShellState;


void shell_state_init(ShellState *state, const char *cwd);

int builtin_is_command(const char *command);

int builtin_hop(
    ShellState *state,
    char *const argv[]
);

int builtin_reveal(
    ShellState *state,
    char *const argv[]
);

int builtin_peek(
    char *const argv[]
);

int builtin_locate(
    char *const argv[]
);

#endif
