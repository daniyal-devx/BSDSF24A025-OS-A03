#include "shell.h"
#include <ctype.h>
#include <readline/history.h>
#include <string.h>

#define MAX_BLOCK_LINES 256

static char* trim_copy(const char* s) {
    if (s == NULL) return NULL;
    while (*s && isspace((unsigned char)*s)) s++;
    const char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    size_t len = (size_t)(end - s);
    char* out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static int is_blank_line(const char* str) {
    if (str == NULL) return 1;
    while (*str) {
        if (!isspace((unsigned char)*str)) return 0;
        str++;
    }
    return 1;
}

static int starts_with_if(const char* line, char** cond_out) {
    const char* s = line;
    while (*s && isspace((unsigned char)*s)) s++;
    if (strncmp(s, "if", 2) != 0) return 0;
    if (s[2] != '\0' && !isspace((unsigned char)s[2])) return 0;
    s += 2;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') {
        *cond_out = NULL;
        return 1; // malformed (no condition) but recognized as if
    }
    *cond_out = strdup(s);
    return 1;
}

static void free_lines(char** lines, int count) {
    for (int i = 0; i < count; i++) free(lines[i]);
}

static int handle_if_structure(char* first_line) {
    char* condition = NULL;
    if (!starts_with_if(first_line, &condition) || condition == NULL) {
        fprintf(stderr, "syntax error: malformed if statement\n");
        free(condition);
        return -1;
    }

    char* then_lines[MAX_BLOCK_LINES];
    char* else_lines[MAX_BLOCK_LINES];
    int then_count = 0, else_count = 0;
    int state = 0; // 0: before then, 1: in then, 2: in else
    int depth = 1;

    // We have consumed the initial if line (already read and added to history by caller)
    while (1) {
        char* line = read_cmd(PROMPT, stdin);
        if (line == NULL) {
            fprintf(stderr, "syntax error: unexpected end of file in if statement\n");
            free(condition);
            free_lines(then_lines, then_count);
            free_lines(else_lines, else_count);
            return -1;
        }

        // add to histories like normal input
        if (line[0] != '\0') add_history(line);
        add_to_history(line);

        char* trimmed = trim_copy(line);
        if (!trimmed) {
            free(line);
            free(condition);
            free_lines(then_lines, then_count);
            free_lines(else_lines, else_count);
            return -1;
        }

        // detect nested if/fi keywords (simple line-based)
        char* dummy_cond = NULL;
        int is_if = starts_with_if(line, &dummy_cond);
        free(dummy_cond);
        int is_then = (strcmp(trimmed, "then") == 0);
        int is_else = (strcmp(trimmed, "else") == 0);
        int is_fi = (strcmp(trimmed, "fi") == 0);
        free(trimmed);

        if (is_if) {
            // nested if: include in current block if appropriate
            if (state == 0) {
                // condition context already set; shouldn't happen
                free(line);
                fprintf(stderr, "syntax error: unexpected if\n");
                free(condition);
                free_lines(then_lines, then_count);
                free_lines(else_lines, else_count);
                return -1;
            } else if (state == 1 && then_count < MAX_BLOCK_LINES) {
                then_lines[then_count++] = line;
            } else if (state == 2 && else_count < MAX_BLOCK_LINES) {
                else_lines[else_count++] = line;
            } else {
                free(line);
            }
            depth++;
            continue;
        }

        if (is_fi) {
            depth--;
            if (depth == 0) {
                free(line);
                break; // end of this if structure
            }
            // nested fi belongs to inner block
            if (state == 1 && then_count < MAX_BLOCK_LINES) {
                then_lines[then_count++] = line;
            } else if (state == 2 && else_count < MAX_BLOCK_LINES) {
                else_lines[else_count++] = line;
            } else {
                free(line);
            }
            continue;
        }

        if (depth == 1 && is_then) {
            if (state != 0) {
                fprintf(stderr, "syntax error: unexpected then\n");
                free(line);
                free(condition);
                free_lines(then_lines, then_count);
                free_lines(else_lines, else_count);
                return -1;
            }
            state = 1;
            free(line);
            continue;
        }

        if (depth == 1 && is_else) {
            if (state != 1) {
                fprintf(stderr, "syntax error: unexpected else\n");
                free(line);
                free(condition);
                free_lines(then_lines, then_count);
                free_lines(else_lines, else_count);
                return -1;
            }
            state = 2;
            free(line);
            continue;
        }

        // regular line: store in appropriate block
        if (state == 0) {
            // expecting then
            if (!is_blank_line(line)) {
                // allow blank lines before then? easier to be flexible; but spec implies structure
                free(line);
                fprintf(stderr, "syntax error: expected then\n");
                free(condition);
                free_lines(then_lines, then_count);
                free_lines(else_lines, else_count);
                return -1;
            }
            free(line);
        } else if (state == 1) {
            if (then_count < MAX_BLOCK_LINES) then_lines[then_count++] = line;
            else free(line);
        } else { // state == 2
            if (else_count < MAX_BLOCK_LINES) else_lines[else_count++] = line;
            else free(line);
        }
    }

    if (state == 0) {
        fprintf(stderr, "syntax error: missing then\n");
        free(condition);
        free_lines(then_lines, then_count);
        free_lines(else_lines, else_count);
        return -1;
    }

    // Execute condition and selected block
    int cond_status = execute_command_get_status(condition);
    free(condition);

    if (cond_status == 0) {
        for (int i = 0; i < then_count; i++) {
            if (!is_blank_line(then_lines[i])) process_command(then_lines[i]);
            free(then_lines[i]);
        }
        for (int i = 0; i < else_count; i++) free(else_lines[i]);
    } else {
        for (int i = 0; i < then_count; i++) free(then_lines[i]);
        for (int i = 0; i < else_count; i++) {
            if (!is_blank_line(else_lines[i])) process_command(else_lines[i]);
            free(else_lines[i]);
        }
    }

    return 0;
}

int main() {
    char* cmdline;

    setup_readline();

    while (1) {
        reap_jobs();
        cmdline = read_cmd(PROMPT, stdin);
        if (cmdline == NULL) {
            break;
        }
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

        char* cond_tmp = NULL;
        if (starts_with_if(line, &cond_tmp)) {
            // cond_tmp allocated if condition present; handle_if_structure will validate/use
            free(cond_tmp);
            handle_if_structure(line);
        } else {
            process_command(cmdline);
        }
        free(cmdline);
    }

    printf("\nShell exited.\n");
    return 0;
}