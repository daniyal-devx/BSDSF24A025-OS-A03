#include "shell.h"
#include <readline/readline.h>
#include <readline/history.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

static char* history[HISTORY_SIZE];
static int history_count = 0;
static int history_start = 0;

static job_t jobs[MAX_JOBS];
static int job_count = 0;

void add_job(pid_t pid, const char* cmd) {
    if (job_count >= MAX_JOBS || cmd == NULL) {
        return;
    }
    jobs[job_count].pid = pid;
    jobs[job_count].cmd = strdup(cmd);
    if (jobs[job_count].cmd == NULL) {
        return;
    }
    job_count++;
}

void remove_job_by_pid(pid_t pid) {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].pid == pid) {
            free(jobs[i].cmd);
            for (int j = i; j < job_count - 1; j++) {
                jobs[j] = jobs[j + 1];
            }
            job_count--;
            return;
        }
    }
}

void reap_jobs(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        remove_job_by_pid(pid);
    }
}

void print_jobs(void) {
    if (job_count == 0) {
        printf("No active background jobs.\n");
        return;
    }
    printf("PID\tCOMMAND\n");
    for (int i = 0; i < job_count; i++) {
        printf("%d\t%s\n", jobs[i].pid, jobs[i].cmd);
    }
}

void add_to_history(const char* cmd) {
    if (cmd == NULL || cmd[0] == '\0') {
        return;
    }

    char* copy = strdup(cmd);
    if (copy == NULL) {
        return;
    }

    if (history_count < HISTORY_SIZE) {
        int index = (history_start + history_count) % HISTORY_SIZE;
        history[index] = copy;
        history_count++;
    } else {
        free(history[history_start]);
        history[history_start] = copy;
        history_start = (history_start + 1) % HISTORY_SIZE;
    }
}

void print_history(void) {
    for (int i = 0; i < history_count; i++) {
        int index = (history_start + i) % HISTORY_SIZE;
        printf("%d  %s\n", i + 1, history[index]);
    }
}

char* get_history_command(int n) {
    if (n < 1 || n > history_count) {
        return NULL;
    }
    int index = (history_start + n - 1) % HISTORY_SIZE;
    return history[index];
}

static char* builtin_commands[] = {
    "cd",
    "exit",
    "help",
    "history",
    "jobs",
    NULL
};

static char* command_generator(const char* text, int state) {
    static int builtin_index;
    static char* path_copy;
    static char* path_token;
    static DIR* dir;
    static size_t len;

    if (state == 0) {
        builtin_index = 0;
        if (path_copy) {
            free(path_copy);
            path_copy = NULL;
        }
        if (dir) {
            closedir(dir);
            dir = NULL;
        }
        path_token = NULL;
        len = strlen(text);

        char* path_env = getenv("PATH");
        if (path_env != NULL) {
            path_copy = strdup(path_env);
            path_token = path_copy ? strtok(path_copy, ":") : NULL;
        }
    }

    while (builtin_commands[builtin_index] != NULL) {
        char* name = builtin_commands[builtin_index++];
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    while (path_token != NULL) {
        if (dir == NULL) {
            dir = opendir(path_token);
            if (dir == NULL) {
                path_token = strtok(NULL, ":");
                continue;
            }
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, text, len) != 0) {
                continue;
            }

            char fullpath[4096];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", path_token, entry->d_name);
            struct stat st;
            if (stat(fullpath, &st) == 0 && S_ISREG(st.st_mode) && access(fullpath, X_OK) == 0) {
                return strdup(entry->d_name);
            }
        }

        closedir(dir);
        dir = NULL;
        path_token = strtok(NULL, ":");
    }

    return NULL;
}

static char** shell_completion(const char* text, int start, int end) {
    (void)end;
    rl_attempted_completion_over = 0;
    if (start == 0) {
        return rl_completion_matches(text, command_generator);
    }
    return rl_completion_matches(text, rl_filename_completion_function);
}

void setup_readline(void) {
    rl_attempted_completion_function = shell_completion;
}

char* read_cmd(char* prompt, FILE* fp) {
    (void)fp;
    char* line = readline(prompt);
    return line;
}

char** tokenize(char* cmdline) {
    // Edge case: empty command line
    if (cmdline == NULL || cmdline[0] == '\0' || cmdline[0] == '\n') {
        return NULL;
    }

    char** arglist = (char**)malloc(sizeof(char*) * (MAXARGS + 1));
    for (int i = 0; i < MAXARGS + 1; i++) {
        arglist[i] = (char*)malloc(sizeof(char) * ARGLEN);
        bzero(arglist[i], ARGLEN);
    }

    char* cp = cmdline;
    int argnum = 0;

    while (*cp != '\0' && argnum < MAXARGS) {
        while (*cp == ' ' || *cp == '\t') cp++; // Skip leading whitespace
        if (*cp == '\0') break; // Line was only whitespace

        int len = 0;
        while (*cp != '\0' && *cp != ' ' && *cp != '\t') {
            if (*cp == '"' || *cp == '\'') {
                char quote = *cp++;
                while (*cp != '\0' && *cp != quote) {
                    if (len < ARGLEN - 1) arglist[argnum][len++] = *cp;
                    cp++;
                }
                if (*cp == quote) cp++;
            } else {
                if (len < ARGLEN - 1) arglist[argnum][len++] = *cp;
                cp++;
            }
        }
        arglist[argnum][len] = '\0';
        argnum++;
    }

    if (argnum == 0) { // No arguments were parsed
        for(int i = 0; i < MAXARGS + 1; i++) free(arglist[i]);
        free(arglist);
        return NULL;
    }

    arglist[argnum] = NULL;
    return arglist;
}

int handle_builtin(char** arglist) {
    if (arglist[0] == NULL) {
        return 0;
    }

    if (strcmp(arglist[0], "exit") == 0) {
        exit(0);
    }

    if (strcmp(arglist[0], "cd") == 0) {
        if (arglist[1] == NULL) {
            fprintf(stderr, "cd: missing argument\n");
        } else if (chdir(arglist[1]) != 0) {
            perror("cd");
        }
        return 1;
    }

    if (strcmp(arglist[0], "help") == 0) {
        printf("Built-in commands:\n");
        printf("  exit                Exit the shell\n");
        printf("  cd <directory>      Change the current working directory\n");
        printf("  help                Display this help message\n");
        printf("  jobs                Display background job status\n");
        printf("  history             Display command history\n");
        return 1;
    }

    if (strcmp(arglist[0], "jobs") == 0) {
        print_jobs();
        return 1;
    }

    if (strcmp(arglist[0], "history") == 0) {
        print_history();
        return 1;
    }

    return 0;
}