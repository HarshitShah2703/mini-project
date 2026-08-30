#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "prompt.h"

#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void prompt_init(char *home, size_t home_size) {
    if (getcwd(home, home_size) == NULL) {
        perror("cshell: getcwd");
        exit(EXIT_FAILURE);
    }
}

static void compute_display_path(const char *home, const char *current, char *out, size_t out_size) {
    size_t home_len = strlen(home);

    if (strcmp(current, home) == 0) {
        snprintf(out, out_size, "~");
        return;
    }

    if (strncmp(current, home, home_len) == 0 && current[home_len] == '/') {
        snprintf(out, out_size, "~%s", current + home_len);
        return;
    }

    snprintf(out, out_size, "%s", current);
}

void prompt_display(const char *home) {
    char current[PATH_MAX];
    char hostname[256];
    char display_path[PATH_MAX];

    if (getcwd(current, sizeof(current)) == NULL) {
        snprintf(current, sizeof(current), "?");
    }

    if (gethostname(hostname, sizeof(hostname)) != 0) {
        snprintf(hostname, sizeof(hostname), "unknown");
    }
    hostname[sizeof(hostname) - 1] = '\0';

    struct passwd *pw = getpwuid(getuid());
    const char *username = (pw != NULL) ? pw->pw_name : "unknown";

    compute_display_path(home, current, display_path, sizeof(display_path));

    printf("<%s@%s:%s> ", username, hostname, display_path);
    fflush(stdout);
}
