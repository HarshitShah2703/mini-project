#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "executor.h"
#include "builtins.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>


static void command_init(Command *cmd) {
    cmd->argv = NULL;
    cmd->argc = 0;
    cmd->infiles = NULL;
    cmd->n_infiles = 0;
    cmd->outfiles = NULL;
    cmd->out_append = NULL;
    cmd->n_outfiles = 0;
}

static void command_push_arg(Command *cmd, const char *word) {
    cmd->argv = realloc(cmd->argv, (size_t)(cmd->argc + 2) * sizeof(char *));
    cmd->argv[cmd->argc] = strdup(word);
    cmd->argc++;
    cmd->argv[cmd->argc] = NULL;
}

static void command_push_infile(Command *cmd, const char *word) {
    cmd->infiles = realloc(cmd->infiles, (size_t)(cmd->n_infiles + 1) * sizeof(char *));
    cmd->infiles[cmd->n_infiles] = strdup(word);
    cmd->n_infiles++;
}

static void command_push_outfile(Command *cmd, const char *word, int append) {
    cmd->outfiles = realloc(cmd->outfiles, (size_t)(cmd->n_outfiles + 1) * sizeof(char *));
    cmd->out_append = realloc(cmd->out_append, (size_t)(cmd->n_outfiles + 1) * sizeof(int));
    cmd->outfiles[cmd->n_outfiles] = strdup(word);
    cmd->out_append[cmd->n_outfiles] = append;
    cmd->n_outfiles++;
}

static void pipeline_push_command(Pipeline *pipeline, Command *cmd) {
    pipeline->commands = realloc(pipeline->commands, (size_t)(pipeline->n_commands + 1) * sizeof(Command));
    pipeline->commands[pipeline->n_commands] = *cmd;
    pipeline->n_commands++;
}

int build_pipeline(const TokenList *tokens, Pipeline *pipeline) {
    pipeline->commands = NULL;
    pipeline->n_commands = 0;

    if (tokens->count == 0) return 0;

    Command current;
    command_init(&current);

    size_t pos = 0;
    while (pos < tokens->count) {
        Token *tok = &tokens->items[pos];

        if (tok->type == TOKEN_WORD) {
            command_push_arg(&current, tok->value);
            pos++;
            continue;
        }

        if (tok->type == TOKEN_LT) {
            pos++; 
            command_push_infile(&current, tokens->items[pos].value);
            pos++;
            continue;
        }

        if (tok->type == TOKEN_GT) {
            pos++;
            command_push_outfile(&current, tokens->items[pos].value, 0);
            pos++;
            continue;
        }

        if (tok->type == TOKEN_GTGT) {
            pos++;
            command_push_outfile(&current, tokens->items[pos].value, 1);
            pos++;
            continue;
        }

        if (tok->type == TOKEN_PIPE) {
            pipeline_push_command(pipeline, &current);
            command_init(&current);
            pos++;
            continue;
        }

        if (tok->type == TOKEN_SEMI || tok->type == TOKEN_AMP) {
            
            pipeline_push_command(pipeline, &current);
            return 0;
        }

        pos++;
    }

    pipeline_push_command(pipeline, &current);
    return 0;
}

void free_pipeline(Pipeline *pipeline) {
    for (int i = 0; i < pipeline->n_commands; i++) {
        Command *cmd = &pipeline->commands[i];
        for (int j = 0; j < cmd->argc; j++) free(cmd->argv[j]);
        free(cmd->argv);
        for (int j = 0; j < cmd->n_infiles; j++) free(cmd->infiles[j]);
        free(cmd->infiles);
        for (int j = 0; j < cmd->n_outfiles; j++) free(cmd->outfiles[j]);
        free(cmd->outfiles);
        free(cmd->out_append);
    }
    free(pipeline->commands);
    pipeline->commands = NULL;
    pipeline->n_commands = 0;
}


static int is_executable_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
    return access(path, X_OK) == 0;
}


static int resolve_command(const char *name, char *out, size_t out_size) {
    if (strchr(name, '/') != NULL) {
        if (is_executable_file(name)) {
            snprintf(out, out_size, "%s", name);
            return 1;
        }
        return 0;
    }

    const char *search_name = name;
    int skip_cwd = 0;

    if (name[0] == '%') {
        search_name = name + 1;
        skip_cwd = 1;
    }

    if (!skip_cwd) {
        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "./%s", search_name);
        if (is_executable_file(candidate)) {
            snprintf(out, out_size, "%s", candidate);
            return 1;
        }
    }

    const char *path_env = getenv("PATH");
    if (path_env == NULL) return 0;

    char *path_copy = strdup(path_env);
    char *saveptr = NULL;
    char *dir = strtok_r(path_copy, ":", &saveptr);
    int found = 0;

    while (dir != NULL) {
        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, search_name);
        if (is_executable_file(candidate)) {
            snprintf(out, out_size, "%s", candidate);
            found = 1;
            break;
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }

    free(path_copy);
    return found;
}



static int apply_input_redirection(Command *cmd) {
    if (cmd->n_infiles == 0) return 0;

    for (int i = 0; i < cmd->n_infiles; i++) {
        if (access(cmd->infiles[i], R_OK) != 0) {
            printf("cshell: no such file or directory\n");
            return -1;
        }
    }

    if (cmd->n_infiles == 1) {
        int fd = open(cmd->infiles[0], O_RDONLY);
        if (fd < 0) {
            printf("cshell: no such file or directory\n");
            return -1;
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
        return 0;
    }


    int fds[2];
    if (pipe(fds) != 0) return -1;

    for (int i = 0; i < cmd->n_infiles; i++) {
        int fd = open(cmd->infiles[i], O_RDONLY);
        if (fd < 0) continue;

        char buffer[65536];
        ssize_t n;
        while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
            ssize_t written = 0;
            while (written < n) {
                ssize_t w = write(fds[1], buffer + written, (size_t)(n - written));
                if (w <= 0) break;
                written += w;
            }
        }
        close(fd);
    }

    close(fds[1]);
    dup2(fds[0], STDIN_FILENO);
    close(fds[0]);
    return 0;
}


static int apply_output_redirection(Command *cmd) {
    if (cmd->n_outfiles == 0) return 0;

    int *fds = malloc((size_t)cmd->n_outfiles * sizeof(int));

    for (int i = 0; i < cmd->n_outfiles; i++) {
        int flags = O_WRONLY | O_CREAT | (cmd->out_append[i] ? O_APPEND : O_TRUNC);
        int fd = open(cmd->outfiles[i], flags, 0644);
        if (fd < 0) {
            printf("cshell: unable to create file for writing\n");
            for (int j = 0; j < i; j++) close(fds[j]);
            free(fds);
            return -1;
        }
        fds[i] = fd;
    }

    if (cmd->n_outfiles == 1) {
        dup2(fds[0], STDOUT_FILENO);
        close(fds[0]);
        free(fds);
        return 0;
    }

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        for (int i = 0; i < cmd->n_outfiles; i++) close(fds[i]);
        free(fds);
        return -1;
    }

    pid_t tee_pid = fork();
    if (tee_pid == 0) {
        close(pipe_fds[1]);
        char buffer[65536];
        ssize_t n;
        while ((n = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
            for (int i = 0; i < cmd->n_outfiles; i++) {
                ssize_t written = 0;
                while (written < n) {
                    ssize_t w = write(fds[i], buffer + written, (size_t)(n - written));
                    if (w <= 0) break;
                    written += w;
                }
            }
        }
        for (int i = 0; i < cmd->n_outfiles; i++) close(fds[i]);
        close(pipe_fds[0]);
        _exit(0);
    }

    for (int i = 0; i < cmd->n_outfiles; i++) close(fds[i]);
    free(fds);
    close(pipe_fds[0]);
    dup2(pipe_fds[1], STDOUT_FILENO);
    close(pipe_fds[1]);
    return 0;
}


static void run_command_and_exit(Command *cmd) {
    if (apply_input_redirection(cmd) != 0) { fflush(stdout); _exit(1); }
    if (apply_output_redirection(cmd) != 0) { fflush(stdout); _exit(1); }

    if (builtin_is_command(cmd->argv[0])) {
        ShellState local_state;
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL) cwd[0] = '\0';
        shell_state_init(&local_state, cwd);

        int status = 0;
        if (strcmp(cmd->argv[0], "hop") == 0) status = builtin_hop(&local_state, cmd->argv);
        else if (strcmp(cmd->argv[0], "reveal") == 0) status = builtin_reveal(&local_state, cmd->argv);
        else if (strcmp(cmd->argv[0], "peek") == 0) status = builtin_peek(cmd->argv);
        else if (strcmp(cmd->argv[0], "locate") == 0) status = builtin_locate(cmd->argv);

        fflush(stdout);
        _exit(status);
    }

    char resolved[PATH_MAX];
    if (!resolve_command(cmd->argv[0], resolved, sizeof(resolved))) {
        printf("cshell: command not found (%s)\n", cmd->argv[0]);
        fflush(stdout);
        _exit(127);
    }

    execv(resolved, cmd->argv);
    printf("cshell: command not found (%s)\n", cmd->argv[0]);
    fflush(stdout);
    _exit(127);
}


static void run_builtin_in_shell(Command *cmd, ShellState *state) {
    int saved_stdin = dup(STDIN_FILENO);
    int saved_stdout = dup(STDOUT_FILENO);

    if (apply_input_redirection(cmd) == 0 && apply_output_redirection(cmd) == 0) {
        if (strcmp(cmd->argv[0], "hop") == 0) builtin_hop(state, cmd->argv);
        else if (strcmp(cmd->argv[0], "reveal") == 0) builtin_reveal(state, cmd->argv);
        else if (strcmp(cmd->argv[0], "peek") == 0) builtin_peek(cmd->argv);
        else if (strcmp(cmd->argv[0], "locate") == 0) builtin_locate(cmd->argv);
    }

    fflush(stdout);
    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdin);
    close(saved_stdout);
}


void execute_pipeline(Pipeline *pipeline, ShellState *state) {
    if (pipeline->n_commands == 0) return;
    if (pipeline->commands[0].argc == 0) return;

    if (pipeline->n_commands == 1 && builtin_is_command(pipeline->commands[0].argv[0])) {
        run_builtin_in_shell(&pipeline->commands[0], state);
        return;
    }

    int n = pipeline->n_commands;
    int (*pipes)[2] = NULL;
    if (n > 1) {
        pipes = malloc((size_t)(n - 1) * sizeof(int[2]));
        for (int i = 0; i < n - 1; i++) {
            if (pipe(pipes[i]) != 0) {
                perror("cshell: pipe");
                free(pipes);
                return;
            }
        }
    }

    pid_t *pids = malloc((size_t)n * sizeof(pid_t));

    for (int i = 0; i < n; i++) {
        pid_t pid = fork();

        if (pid == 0) {
            
            if (i > 0) dup2(pipes[i - 1][0], STDIN_FILENO);
            if (i < n - 1) dup2(pipes[i][1], STDOUT_FILENO);

            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            run_command_and_exit(&pipeline->commands[i]);
        }

        if (pid < 0) {
            perror("cshell: fork");
        }

        pids[i] = pid;
    }

    for (int j = 0; j < n - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
    }

    for (int i = 0; i < n; i++) {
        if (pids[i] > 0) {
            int status;
            waitpid(pids[i], &status, 0);
        }
    }

    free(pids);
    free(pipes);
}
