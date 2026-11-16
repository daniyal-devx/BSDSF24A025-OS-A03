#include "shell.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>

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
            i++;
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
        free(cmd->argv);
        cmd->argv = NULL;
    }
}

static void trim_inplace(char* str) {
    if (str == NULL) return;
    char* start = str;
    while (*start && isspace((unsigned char)*start)) start++;
    char* end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
    if (start != str) memmove(str, start, strlen(start) + 1);
}

static int is_blank(const char* str) {
    if (str == NULL) return 1;
    while (*str) {
        if (!isspace((unsigned char)*str)) return 0;
        str++;
    }
    return 1;
}

static int parse_background(char* segment, char* jobcmd_out, size_t outsz) {
    // trim leading/trailing
    trim_inplace(segment);
    if (segment[0] == '\0') return 0;

    // save original trimmed command for job display
    strncpy(jobcmd_out, segment, outsz - 1);
    jobcmd_out[outsz - 1] = '\0';

    int background = 0;
    size_t len = strlen(segment);
    if (len > 0 && segment[len - 1] == '&') {
        background = 1;
        segment[len - 1] = '\0';
        trim_inplace(segment);
        // also trim trailing & in job display? keep command text without &
        size_t jlen = strlen(jobcmd_out);
        if (jlen > 0 && jobcmd_out[jlen - 1] == '&') {
            jobcmd_out[jlen - 1] = '\0';
            trim_inplace(jobcmd_out);
        }
    }
    return background;
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
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            return 1;
    }
}

static int execute_simple_cmd(command_t* cmd, int background, const char* jobcmd) {
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
    if (background) {
        add_job(pid, jobcmd);
        printf("[%d] %d\n", 1, pid);
        return 0;
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        return 1;
    }
}

static int execute_pipe_cmd(command_t* left, command_t* right, int background, const char* jobcmd) {
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
        if (background) {
            add_job(left_pid, jobcmd);
        } else {
            waitpid(left_pid, NULL, 0);
        }
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
    if (background) {
        add_job(left_pid, jobcmd);
        add_job(right_pid, jobcmd);
        printf("[%d] %d %d\n", 1, left_pid, right_pid);
        return 0;
    } else {
        int status_left = 0, status_right = 0;
        waitpid(left_pid, &status_left, 0);
        waitpid(right_pid, &status_right, 0);
        if (WIFEXITED(status_right)) return WEXITSTATUS(status_right);
        return 1;
    }
}

static int execute_segment(char* segment, int* last_status) {
    char jobcmd[MAX_LEN];
    int background = parse_background(segment, jobcmd, sizeof(jobcmd));

    if (is_blank(segment)) {
        return 0; // empty command
    }

    // Fast path: no special chars -> preserve existing simple behavior
    if (!has_special_chars(segment)) {
        char** arglist = tokenize(segment);
        if (arglist != NULL) {
            if (!handle_builtin(arglist)) {
                if (background) {
                    pid_t pid = fork();
                    if (pid < 0) {
                        perror("fork failed");
                    } else if (pid == 0) {
                        execvp(arglist[0], arglist);
                        perror(arglist[0]);
                        exit(127);
                    } else {
                        add_job(pid, jobcmd);
                        printf("[%d] %d\n", 1, pid);
                        if (last_status) *last_status = 0;
                    }
                } else {
                    int st = execute(arglist);
                    if (last_status) *last_status = st;
                }
            } else {
                if (last_status) *last_status = 0; // builtin handled
            }
            for (int i = 0; arglist[i] != NULL; i++) {
                free(arglist[i]);
            }
            free(arglist);
        }
        return 0;
    }

    int ntokens = 0;
    char** tokens = tokenize_special(segment, &ntokens);
    if (tokens == NULL || ntokens == 0) {
        if (tokens) free_tokens(tokens, ntokens);
        return 0;
    }

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
            if (left.argv) free(left.argv);
            if (right.argv) free(right.argv);
            free_tokens(tokens, ntokens);
            return -1;
        }
        int st = execute_pipe_cmd(&left, &right, background, jobcmd);
        if (last_status) *last_status = st;
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
        int st = execute_simple_cmd(&cmd, background, jobcmd);
        if (last_status) *last_status = st;
        free_command(&cmd);
        free_tokens(tokens, ntokens);
        return 0;
    }
}

int process_command(char* cmdline) {
    if (cmdline == NULL) return 0;

    // handle semicolon chaining
    char* linecopy = strdup(cmdline);
    if (linecopy == NULL) return -1;

    char* saveptr = NULL;
    char* segment = strtok_r(linecopy, ";", &saveptr);
    while (segment != NULL) {
        char* segcopy = strdup(segment);
        if (segcopy != NULL) {
            execute_segment(segcopy, NULL);
            free(segcopy);
        }
        segment = strtok_r(NULL, ";", &saveptr);
    }

    free(linecopy);
    return 0;
}

int execute_command_get_status(char* cmdline) {
    if (cmdline == NULL) return 1;

    char* linecopy = strdup(cmdline);
    if (linecopy == NULL) return 1;

    int last_status = 0;
    char* saveptr = NULL;
    char* segment = strtok_r(linecopy, ";", &saveptr);
    while (segment != NULL) {
        char* segcopy = strdup(segment);
        if (segcopy != NULL) {
            execute_segment(segcopy, &last_status);
            free(segcopy);
        }
        segment = strtok_r(NULL, ";", &saveptr);
    }

    free(linecopy);
    return last_status;
}