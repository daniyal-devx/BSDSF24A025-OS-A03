#include "shell.h"
#include <ctype.h>
#include <readline/history.h>

int main() {
    char* cmdline;
    char** arglist;

    setup_readline();

    while ((cmdline = read_cmd(PROMPT, stdin)) != NULL) {
        char* line = cmdline;

        if (line[0] == '!') {
            if (line[1] == '\0') {
                fprintf(stderr, "history: invalid history reference\n");
                free(cmdline);
                continue;
            }

            char* endptr;
            long n = strtol(line + 1, &endptr, 10);
            if (*endptr != '\0') {
                fprintf(stderr, "history: invalid history reference\n");
                free(cmdline);
                continue;
            }

            char* hist_cmd = get_history_command((int)n);
            if (hist_cmd == NULL) {
                fprintf(stderr, "history: %ld: event not found\n", n);
                free(cmdline);
                continue;
            }

            char* expanded = strdup(hist_cmd);
            if (expanded == NULL) {
                perror("strdup");
                free(cmdline);
                continue;
            }

            free(cmdline);
            cmdline = expanded;
            line = cmdline;
        }

        if (line[0] != '\0') {
            add_history(line);
        }
        add_to_history(line);

        if ((arglist = tokenize(cmdline)) != NULL) {
            if (!handle_builtin(arglist)) {
                execute(arglist);
            }

            // Free the memory allocated by tokenize()
            for (int i = 0; arglist[i] != NULL; i++) {
                free(arglist[i]);
            }
            free(arglist);
        }
        free(cmdline);
    }

    printf("\nShell exited.\n");
    return 0;
}