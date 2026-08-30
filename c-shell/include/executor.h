#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "lexer.h"
#include "builtins.h"

typedef struct {
    char **argv;      
    int argc;

    char **infiles;
    int n_infiles;

    char **outfiles;
    int *out_append;  
    int n_outfiles;
} Command;

typedef struct {
    Command *commands;
    int n_commands;
} Pipeline;

int build_pipeline(const TokenList *tokens, Pipeline *pipeline);

void free_pipeline(Pipeline *pipeline);


void execute_pipeline(Pipeline *pipeline, ShellState *state);

#endif
