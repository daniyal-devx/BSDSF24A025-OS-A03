#include "shell.h"
#include <fcntl.h>
#include <sys/stat.h>

#define MAX_TOKENS 128

typedef struct {
    char** argv;
    char* infile;
    char* outfile;
} command_t;

static int is_special(char c) {
    return c == '<' || c == '>' || c == '|';
}

static int has_special_chars(const char* cmdline) {
    for (int i = 0; cmdline[i] != '\0'; i++) {
        if (cmdline[i] == '<' || cmdline[i] == '>' || cmdline[i] == '|') {
            return 1;
        }
    }
    return 0;
}

/* helpers removed */

static char** tokenize_special(const char* cmdline, int* count) {
    char** tokens = malloc((MAX_TOKENS + 1) * sizeof(char*));
    if (tokens == NULL) {
        *count = 0;
        return NULL;
    }
    int n = 0;
    int i = 0;
    while (cmdline[i] != '\0') {
        while (cmdline[i] == ' ' || cmdline[i] == '\t') i++;
        if (cmdline[i] == '\0') break;
        if (n >= MAX_TOKENS) break;

        if (is_special(cmdline[i])) {
            char op[2] = {cmdline[i], '\0'};
            tokens[n] = strdup(op);
            if (tokens[n] == NULL) {
                for (int k = 0; k < n; k++) free(tokens[k]);
                free(tokens);
                *count = 0;
                return NULL;
            }
            n++;
            i++;
        } else {
            char buf[MAX_LEN];
            int bi = 0;
            while (cmdline[i] != '\0' && cmdline[i] != ' ' && cmdline[i] != '\t' && !is_special(cmdline[i])) {
                if (cmdline[i] == '"' || cmdline[i] == '\'') {
                    char quote = cmdline[i++];
                    while (cmdline[i] != '\0' && cmdline[i] != quote) {
                        if (bi < MAX_LEN - 1) buf[bi++] = cmdline[i];
                        i++;
                    }
                    if (cmdline[i] == quote) i++;
                } else {
                    if (bi < MAX_LEN - 1) buf[bi++] = cmdline[i];
                    i++;
                }
            }
            buf[bi] = '\0';
            tokens[n] = strdup(buf);
            if (tokens[n] == NULL) {
                for (int k = 0; k < n; k++) free(tokens[k]);
                free(tokens);
                *count = 0;
                return NULL;
            }
            n++;
        }
    }
    tokens[n] = NULL;
    *count = n;
    return tokens;
}

static void free_tokens(char** tokens, int count) {
    if (tokens == NULL) return;
    for (int i = 0; i < count; i++) free(tokens[i]);
    free(tokens);
}

static int build_command(char** tokens, int start, int end, command_t* cmd) {
    cmd->argv = NULL;
    cmd->infile = NULL;
    cmd->outfile = NULL;

    int argc = 0;
    for (int i = start; i < end; i++) {
        if (strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0) {
            if (i + 1 >= end) {
                fprintf(stderr, "syntax error: missing filename for redirection\n");
                return -1;
            }
            if (is_special(tokens[i + 1][0])) {
                fprintf(stderr, "syntax error: invalid redirection target\n");
                return -1;
            }
            if (strcmp(tokens[i], "<") == 0) {
                if (cmd->infile != NULL) {
                    fprintf(stderr, "syntax error: multiple input redirections\n");
                    return -1;
                }
                cmd->infile = tokens[i + 1];
            } else {
                if (cmd->outfile != NULL) {
                    fprintf(stderr, "syntax error: multiple output redirections\n");
                    return -1;
                }
                cmd->outfile = tokens[i + 1];
            }
            i++; // skip filename
        } else if (strcmp(tokens[i], "|") == 0) {
            fprintf(stderr, "syntax error: unexpected pipe\n");
            return -1;
        } else {
            argc++;
        }
    }

    if (argc == 0) {
        fprintf(stderr, "syntax error: missing command\n");
        return -1;
    }

    cmd->argv = malloc((argc + 1) * sizeof(char*));
    if (cmd->argv == NULL) {
        perror("malloc");
        return -1;
    }
    int j = 0;
    for (int i = start; i < end; i++) {
        if (strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0) {
            i++;
            continue;
        } else {
            cmd->argv[j++] = tokens[i];
        }
    }
    cmd->argv[j] = NULL;
    return 0;
}

static int setup_redirections(command_t* cmd) {
    if (cmd->infile != NULL) {
        int fd = open(cmd->infile, O_RDONLY);
        if (fd < 0) {
            perror(cmd->infile);
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }
    if (cmd->outfile != NULL) {
        int fd = open(cmd->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror(cmd->outfile);
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

static void free_command(command_t* cmd) {
    if (cmd->argv != NULL) {
        free(cmd->argv); // elements owned by tokens array
        cmd->argv = NULL;
    }
}

int execute(char* arglist[]) {
    int status;
    int cpid = fork();

    switch (cpid) {
        case -1:
            perror("fork failed");
            return -1;
        case 0:
            execvp(arglist[0], arglist);
            perror(arglist[0]);
            exit(127);
        default:
            waitpid(cpid, &status, 0);
            return 0;
    }
}

static int execute_simple(command_t* cmd) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return -1;
    }
    if (pid == 0) {
        if (setup_redirections(cmd) < 0) {
            exit(1);
        }
        execvp(cmd->argv[0], cmd->argv);
        perror(cmd->argv[0]);
        exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return 0;
}

static int execute_pipe(command_t* left, command_t* right) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return -1;
    }

    pid_t left_pid = fork();
    if (left_pid < 0) {
        perror("fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (left_pid == 0) {
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            perror("dup2");
            exit(1);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        if (setup_redirections(left) < 0) {
            exit(1);
        }
        execvp(left->argv[0], left->argv);
        perror(left->argv[0]);
        exit(127);
    }

    pid_t right_pid = fork();
    if (right_pid < 0) {
        perror("fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        waitpid(left_pid, NULL, 0);
        return -1;
    }
    if (right_pid == 0) {
        if (dup2(pipefd[0], STDIN_FILENO) < 0) {
            perror("dup2");
            exit(1);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        if (setup_redirections(right) < 0) {
            exit(1);
        }
        execvp(right->argv[0], right->argv);
        perror(right->argv[0]);
        exit(127);
    }

    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(left_pid, NULL, 0);
    waitpid(right_pid, NULL, 0);
    return 0;
}

int process_command(char* cmdline) {
    if (cmdline == NULL) return 0;

    // Fast path: no special chars -> preserve existing simple behavior
    if (!has_special_chars(cmdline)) {
        char** arglist = tokenize(cmdline);
        if (arglist != NULL) {
            if (!handle_builtin(arglist)) {
                execute(arglist);
            }
            for (int i = 0; arglist[i] != NULL; i++) {
                free(arglist[i]);
            }
            free(arglist);
        }
        return 0;
    }

    int ntokens = 0;
    char** tokens = tokenize_special(cmdline, &ntokens);
    if (tokens == NULL || ntokens == 0) {
        if (tokens) free_tokens(tokens, ntokens);
        return 0;
    }

    // find pipe
    int pipe_index = -1;
    for (int i = 0; i < ntokens; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            if (pipe_index != -1) {
                fprintf(stderr, "syntax error: multiple pipes not supported\n");
                free_tokens(tokens, ntokens);
                return -1;
            }
            pipe_index = i;
        }
    }

    if (pipe_index != -1) {
        if (pipe_index == 0 || pipe_index == ntokens - 1) {
            fprintf(stderr, "syntax error: invalid pipe usage\n");
            free_tokens(tokens, ntokens);
            return -1;
        }
        command_t left, right;
        if (build_command(tokens, 0, pipe_index, &left) < 0 ||
            build_command(tokens, pipe_index + 1, ntokens, &right) < 0) {
            // build_command may have partially set argv on failure? it returns -1 before alloc in some cases
            if (left.argv) free(left.argv);
            if (right.argv) free(right.argv);
            free_tokens(tokens, ntokens);
            return -1;
        }
        execute_pipe(&left, &right);
        free_command(&left);
        free_command(&right);
        free_tokens(tokens, ntokens);
        return 0;
    } else {
        command_t cmd;
        if (build_command(tokens, 0, ntokens, &cmd) < 0) {
            if (cmd.argv) free(cmd.argv);
            free_tokens(tokens, ntokens);
            return -1;
        }
        // For simple external commands with redirection, do not treat as builtin
        execute_simple(&cmd);
        free_command(&cmd);
        free_tokens(tokens, ntokens);
        return 0;
    }
}