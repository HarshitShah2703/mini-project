#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "builtins.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>



void shell_state_init(ShellState *state, const char *home) {
    snprintf(state->home_directory, sizeof(state->home_directory), "%s", home);
    state->previous_directory[0] = '\0';
    state->has_previous_directory = 0;
}

int builtin_is_command(const char *name) {
    return strcmp(name, "hop") == 0 ||
           strcmp(name, "reveal") == 0 ||
           strcmp(name, "peek") == 0 ||
           strcmp(name, "locate") == 0;
}

static int argc_of(char *const argv[]) {
    int n = 0;
    while (argv[n] != NULL) n++;
    return n;
}

static int cmp_str(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}



typedef struct {
    char path[PATH_MAX];
    long freq;
    long last_seq;
} FrecencyEntry;

static const char *frecency_file_path(char *buffer, size_t size) {
    const char *home = getenv("HOME");
    if (home == NULL) home = "/tmp";
    snprintf(buffer, size, "%s/.cshell_frecency", home);
    return buffer;
}

static int load_frecency(FrecencyEntry **entries_out, size_t *count_out, long *next_seq_out) {
    char path[PATH_MAX];
    frecency_file_path(path, sizeof(path));

    *entries_out = NULL;
    *count_out = 0;
    *next_seq_out = 1;

    FILE *f = fopen(path, "r");
    if (f == NULL) return 0;

    char line[PATH_MAX + 64];

    if (fgets(line, sizeof(line), f) != NULL) {
        long seq = 1;
        if (sscanf(line, "SEQ %ld", &seq) == 1) {
            *next_seq_out = seq;
        }
    }

    size_t capacity = 0, count = 0;
    FrecencyEntry *entries = NULL;

    while (fgets(line, sizeof(line), f) != NULL) {
        long freq, seq;
        char entry_path[PATH_MAX];
        if (sscanf(line, "%ld %ld %[^\n]", &freq, &seq, entry_path) != 3) continue;

        if (count == capacity) {
            capacity = capacity == 0 ? 16 : capacity * 2;
            entries = realloc(entries, capacity * sizeof(FrecencyEntry));
        }
        entries[count].freq = freq;
        entries[count].last_seq = seq;
        snprintf(entries[count].path, sizeof(entries[count].path), "%s", entry_path);
        count++;
    }

    fclose(f);
    *entries_out = entries;
    *count_out = count;
    return 0;
}

static void save_frecency(FrecencyEntry *entries, size_t count, long next_seq) {
    char path[PATH_MAX];
    frecency_file_path(path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (f == NULL) return;

    fprintf(f, "SEQ %ld\n", next_seq);
    for (size_t i = 0; i < count; i++) {
        fprintf(f, "%ld %ld %s\n", entries[i].freq, entries[i].last_seq, entries[i].path);
    }

    fclose(f);
}


static void record_frecency(const char *abs_path) {
    FrecencyEntry *entries = NULL;
    size_t count = 0;
    long next_seq = 1;

    load_frecency(&entries, &count, &next_seq);

    size_t found = count;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(entries[i].path, abs_path) == 0) {
            found = i;
            break;
        }
    }

    if (found == count) {
        entries = realloc(entries, (count + 1) * sizeof(FrecencyEntry));
        snprintf(entries[count].path, sizeof(entries[count].path), "%s", abs_path);
        entries[count].freq = 0;
        entries[count].last_seq = 0;
        count++;
        found = count - 1;
    }

    entries[found].freq += 1;
    entries[found].last_seq = next_seq;
    next_seq += 1;

    save_frecency(entries, count, next_seq);
    free(entries);
}

static long frecency_score(const FrecencyEntry *e) {
    return e->freq * 1000000L + e->last_seq;
}

static int frecency_lookup(const char *needle, char *out, size_t out_size) {
    FrecencyEntry *entries = NULL;
    size_t count = 0;
    long next_seq = 1;

    load_frecency(&entries, &count, &next_seq);
    if (count == 0) return 0;

    int *disqualified = calloc(count, sizeof(int));
    int found = 0;

    for (size_t attempt = 0; attempt < count && !found; attempt++) {
        long best_score = -1;
        size_t best_index = count;

        for (size_t i = 0; i < count; i++) {
            if (disqualified[i]) continue;
            if (strstr(entries[i].path, needle) == NULL) continue;

            long score = frecency_score(&entries[i]);
            if (score > best_score) {
                best_score = score;
                best_index = i;
            }
        }

        if (best_index == count) break; 

        struct stat st;
        if (stat(entries[best_index].path, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(out, out_size, "%s", entries[best_index].path);
            found = 1;
        } else {
            disqualified[best_index] = 1;
        }
    }

    free(disqualified);
    free(entries);
    return found;
}



int builtin_hop(ShellState *state, char *const argv[]) {
    int argc = argc_of(argv);

    if (argc == 1) {
        
        char old_cwd[PATH_MAX];
        if (getcwd(old_cwd, sizeof(old_cwd)) == NULL) old_cwd[0] = '\0';

        if (chdir(state->home_directory) == 0) {
            if (old_cwd[0] != '\0') {
                snprintf(state->previous_directory, sizeof(state->previous_directory), "%s", old_cwd);
                state->has_previous_directory = 1;
            }
            record_frecency(state->home_directory);
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        const char *token = argv[i];
        char old_cwd[PATH_MAX];
        if (getcwd(old_cwd, sizeof(old_cwd)) == NULL) old_cwd[0] = '\0';

        char target[PATH_MAX];
        int have_target = 0;

        if (strcmp(token, "~") == 0) {
            snprintf(target, sizeof(target), "%s", state->home_directory);
            have_target = 1;
        } else if (strcmp(token, ".") == 0) {
            continue; /* stay */
        } else if (strcmp(token, "..") == 0) {
            snprintf(target, sizeof(target), "..");
            have_target = 1;
        } else if (strcmp(token, "-") == 0) {
            if (!state->has_previous_directory) continue; /* do nothing */
            snprintf(target, sizeof(target), "%s", state->previous_directory);
            have_target = 1;
        } else {
            
            struct stat st;
            if (stat(token, &st) == 0 && S_ISDIR(st.st_mode)) {
                snprintf(target, sizeof(target), "%s", token);
                have_target = 1;
            } else {
                char resolved[PATH_MAX];
                if (frecency_lookup(token, resolved, sizeof(resolved))) {
                    snprintf(target, sizeof(target), "%s", resolved);
                    have_target = 1;
                } else {
                    printf("hop: no such directory\n");
                    have_target = 0;
                }
            }
        }

        if (!have_target) continue;

        if (chdir(target) != 0) {
            printf("hop: no such directory\n");
            continue;
        }

        char new_cwd[PATH_MAX];
        if (getcwd(new_cwd, sizeof(new_cwd)) != NULL) {
            record_frecency(new_cwd);
        }

        if (old_cwd[0] != '\0') {
            snprintf(state->previous_directory, sizeof(state->previous_directory), "%s", old_cwd);
            state->has_previous_directory = 1;
        }
    }

    return 0;
}



static void reveal_walk(const char *abs_dir, const char *display_prefix, int show_all, int recursive) {
    DIR *dir = opendir(abs_dir);
    if (dir == NULL) return;

    char **names = NULL;
    size_t count = 0, capacity = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (!show_all && entry->d_name[0] == '.') continue;

        if (count == capacity) {
            capacity = capacity == 0 ? 16 : capacity * 2;
            names = realloc(names, capacity * sizeof(char *));
        }
        names[count++] = strdup(entry->d_name);
    }
    closedir(dir);

    qsort(names, count, sizeof(char *), cmp_str);

    for (size_t i = 0; i < count; i++) {
        char abs_child[PATH_MAX];
        snprintf(abs_child, sizeof(abs_child), "%s/%s", abs_dir, names[i]);

        struct stat st;
        int is_dir = (stat(abs_child, &st) == 0) && S_ISDIR(st.st_mode);

        char display_child[PATH_MAX];
        if (display_prefix[0] == '\0') {
            snprintf(display_child, sizeof(display_child), "%s", names[i]);
        } else {
            snprintf(display_child, sizeof(display_child), "%s/%s", display_prefix, names[i]);
        }

        if (is_dir) {
            printf("%s/\n", display_child);
        } else {
            printf("%s\n", display_child);
        }

        if (is_dir && recursive) {
            reveal_walk(abs_child, display_child, show_all, recursive);
        }

        free(names[i]);
    }

    free(names);
}

int builtin_reveal(ShellState *state, char *const argv[]) {
    int argc = argc_of(argv);

    int show_all = 0;
    int recursive = 0;
    const char *target_arg = NULL;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg[0] == '-' && arg[1] != '\0') {
            for (const char *p = arg + 1; *p != '\0'; p++) {
                if (*p == 'a') show_all = 1;
                else if (*p == 't') recursive = 1;
                else { printf("reveal: invalid syntax\n"); return 1; }
            }
        } else {
            if (target_arg != NULL) {
                printf("reveal: invalid syntax\n");
                return 1;
            }
            target_arg = arg;
        }
    }

    char abs_dir[PATH_MAX + 16];

    if (target_arg == NULL || strcmp(target_arg, ".") == 0) {
        if (getcwd(abs_dir, sizeof(abs_dir)) == NULL) {
            printf("reveal: no such directory\n");
            return 1;
        }
    } else if (strcmp(target_arg, "~") == 0) {
        snprintf(abs_dir, sizeof(abs_dir), "%s", state->home_directory);
    } else if (strcmp(target_arg, "..") == 0) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL) { printf("reveal: no such directory\n"); return 1; }
        snprintf(abs_dir, sizeof(abs_dir), "%s/..", cwd);
    } else if (strcmp(target_arg, "-") == 0) {
        if (!state->has_previous_directory) {
            printf("reveal: no such directory\n");
            return 1;
        }
        snprintf(abs_dir, sizeof(abs_dir), "%s", state->previous_directory);
    } else {
        snprintf(abs_dir, sizeof(abs_dir), "%s", target_arg);
    }

    struct stat st;
    if (stat(abs_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        printf("reveal: no such directory\n");
        return 1;
    }

    reveal_walk(abs_dir, "", show_all, recursive);
    return 0;
}



static char *read_all(int fd, size_t *len) {
    size_t capacity = 65536;
    size_t used = 0;
    char *buffer = malloc(capacity);
    if (buffer == NULL) return NULL;

    ssize_t n;
    while ((n = read(fd, buffer + used, capacity - used)) > 0) {
        used += (size_t)n;
        if (used == capacity) {
            capacity *= 2;
            char *new_buffer = realloc(buffer, capacity);
            if (new_buffer == NULL) { free(buffer); return NULL; }
            buffer = new_buffer;
        }
    }

    *len = used;
    return buffer;
}


typedef struct { const char *start; size_t len; } LineSpan;

static LineSpan *split_lines(const char *buffer, size_t len, size_t *line_count) {
    size_t capacity = 0, count = 0;
    LineSpan *lines = NULL;

    size_t line_start = 0;
    for (size_t i = 0; i < len; i++) {
        if (buffer[i] == '\n') {
            if (count == capacity) {
                capacity = capacity == 0 ? 16 : capacity * 2;
                lines = realloc(lines, capacity * sizeof(LineSpan));
            }
            lines[count].start = buffer + line_start;
            lines[count].len = i - line_start;
            count++;
            line_start = i + 1;
        }
    }
    if (line_start < len) {
        if (count == capacity) {
            capacity = capacity == 0 ? 16 : capacity * 2;
            lines = realloc(lines, capacity * sizeof(LineSpan));
        }
        lines[count].start = buffer + line_start;
        lines[count].len = len - line_start;
        count++;
    }

    *line_count = count;
    return lines;
}

static void peek_emit(const LineSpan *lines, size_t count, int reverse, int number) {
    long line_no = 0;
    if (number) {
        for (size_t i = 0; i < count; i++) if (lines[i].len > 0) line_no++;
    }

    if (!reverse) {
        long current = 0;
        for (size_t i = 0; i < count; i++) {
            if (number) {
                if (lines[i].len > 0) {
                    current++;
                    printf("%ld %.*s\n", current, (int)lines[i].len, lines[i].start);
                } else {
                    printf("\n");
                }
            } else {
                printf("%.*s\n", (int)lines[i].len, lines[i].start);
            }
        }
    } else {
        long current = line_no;
        for (size_t idx = count; idx > 0; idx--) {
            size_t i = idx - 1;
            if (number) {
                if (lines[i].len > 0) {
                    printf("%ld %.*s\n", current, (int)lines[i].len, lines[i].start);
                    current--;
                } else {
                    printf("\n");
                }
            } else {
                printf("%.*s\n", (int)lines[i].len, lines[i].start);
            }
        }
    }
}

static int peek_process_fd(int fd, int reverse, int number) {
    size_t len = 0;
    char *buffer = read_all(fd, &len);
    if (buffer == NULL) return 1;

    size_t line_count = 0;
    LineSpan *lines = split_lines(buffer, len, &line_count);

    peek_emit(lines, line_count, reverse, number);

    free(lines);
    free(buffer);
    return 0;
}

int builtin_peek(char *const argv[]) {
    int argc = argc_of(argv);

    int reverse = 0, number = 0;
    char **files = malloc((size_t)argc * sizeof(char *));
    int file_count = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg[0] == '-' && arg[1] != '\0' && strcmp(arg, "-") != 0) {
            for (const char *p = arg + 1; *p != '\0'; p++) {
                if (*p == 'n') number = 1;
                else if (*p == 'r') reverse = 1;
            }
        } else {
            files[file_count++] = (char *)arg;
        }
    }

    int overall_status = 0;

    if (file_count == 0) {
        overall_status |= peek_process_fd(STDIN_FILENO, reverse, number);
    } else {
        for (int i = 0; i < file_count; i++) {
            const char *filename = files[i];

            if (strcmp(filename, "-") == 0) {
                overall_status |= peek_process_fd(STDIN_FILENO, reverse, number);
                continue;
            }

            struct stat st;
            if (stat(filename, &st) != 0) {
                printf("peek: no such file or directory\n");
                overall_status = 1;
                continue;
            }
            if (S_ISDIR(st.st_mode)) {
                printf("peek: is a directory\n");
                overall_status = 1;
                continue;
            }

            int fd = open(filename, O_RDONLY);
            if (fd < 0) {
                printf("peek: no such file or directory\n");
                overall_status = 1;
                continue;
            }

            overall_status |= peek_process_fd(fd, reverse, number);
            close(fd);
        }
    }

    free(files);
    return overall_status;
}



static void locate_one(const char *name) {
    int printed_any = 0;
    char abs_path[PATH_MAX];

    struct stat st;
    if (stat(name, &st) == 0 && (st.st_mode & S_IXUSR)) {
        if (realpath(name, abs_path) != NULL) {
            printf("%s\n", abs_path);
            printed_any = 1;
        }
    }
    
    const char *path_env = getenv("PATH");
    if (path_env != NULL) {
        char *path_copy = strdup(path_env);
        char *saveptr = NULL;
        char *dir = strtok_r(path_copy, ":", &saveptr);

        while (dir != NULL) {
            char candidate[PATH_MAX];
            snprintf(candidate, sizeof(candidate), "%s/%s", dir, name);

            struct stat cst;
            if (stat(candidate, &cst) == 0 && S_ISREG(cst.st_mode) && (cst.st_mode & S_IXUSR)) {
                if (realpath(candidate, abs_path) != NULL) {
                    printf("%s\n", abs_path);
                    printed_any = 1;
                }
            }

            dir = strtok_r(NULL, ":", &saveptr);
        }

        free(path_copy);
    }

    if (!printed_any) {
        printf("locate: command not found (%s)\n", name);
    }
}

int builtin_locate(char *const argv[]) {
    int argc = argc_of(argv);

    if (argc < 2) {
        printf("locate: invalid syntax\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        locate_one(argv[i]);
    }

    return 0;
}
