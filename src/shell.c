#include "shell.h"
#include <readline/readline.h>
#include <readline/history.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>

static char* history[HISTORY_SIZE];
static int history_count = 0;
static int history_start = 0;

static job_t jobs[MAX_JOBS];
static int job_count = 0;

typedef struct var_node {
    char* name;
    char* value;
    struct var_node* next;
} var_node;

static var_node* var_list = NULL;

static int is_valid_var_name(const char* name) {
    if (name == NULL || name[0] == '\0') return 0;
    if (!(isalpha((unsigned char)name[0]) || name[0] == '_')) return 0;
    for (int i = 1; name[i] != '\0'; i++) {
        if (!(isalnum((unsigned char)name[i]) || name[i] == '_')) return 0;
    }
    return 1;
}

int is_assignment(const char* arg) {
    if (arg == NULL) return 0;
    const char* eq = strchr(arg, '=');
    if (eq == NULL || eq == arg) return 0;
    char name[256];
    size_t nlen = (size_t)(eq - arg);
    if (nlen >= sizeof(name)) return 0;
    memcpy(name, arg, nlen);
    name[nlen] = '\0';
    return is_valid_var_name(name);
}

int handle_assignment(const char* arg) {
    if (!is_assignment(arg)) return 0;
    const char* eq = strchr(arg, '=');
    size_t nlen = (size_t)(eq - arg);
    char* name = malloc(nlen + 1);
    if (!name) return 1;
    memcpy(name, arg, nlen);
    name[nlen] = '\0';

    const char* val_src = eq + 1;
    char* value = strdup(val_src ? val_src : "");
    if (!value) {
        free(name);
        return 1;
    }

    // strip surrounding quotes if present
    size_t vlen = strlen(value);
    if (vlen >= 2 && ((value[0] == '"' && value[vlen - 1] == '"') || (value[0] == '\'' && value[vlen - 1] == '\''))) {
        char* unquoted = malloc(vlen - 1);
        if (unquoted) {
            memcpy(unquoted, value + 1, vlen - 2);
            unquoted[vlen - 2] = '\0';
            free(value);
            value = unquoted;
        }
    }

    // update existing
    for (var_node* cur = var_list; cur != NULL; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            char* newv = strdup(value);
            if (newv) {
                free(cur->value);
                cur->value = newv;
            }
            free(name);
            free(value);
            return 1;
        }
    }

    // add new
    var_node* node = malloc(sizeof(var_node));
    if (!node) {
        free(name);
        free(value);
        return 1;
    }
    node->name = name;
    node->value = value;
    node->next = var_list;
    var_list = node;
    return 1;
}

static const char* get_variable_value(const char* name) {
    for (var_node* cur = var_list; cur != NULL; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            return cur->value;
        }
    }
    return NULL;
}

void expand_variables(char** arglist) {
    if (arglist == NULL) return;
    for (int i = 0; arglist[i] != NULL; i++) {
        if (arglist[i][0] == '$' && arglist[i][1] != '\0') {
            const char* name = arglist[i] + 1;
            const char* val = get_variable_value(name);
            char* newval = strdup(val ? val : "");
            if (newval) {
                free(arglist[i]);
                arglist[i] = newval;
            }
        }
    }
}

void print_variables(void) {
    if (var_list == NULL) {
        printf("No variables defined.\n");
        return;
    }
    // print in reverse? list prepends; acceptable either way
    // to print in insertion-like order, collect or reverse - simple reverse traversal by recursion/count not necessary
    // better to print in defined order by reversing into temp array
    int count = 0;
    for (var_node* cur = var_list; cur != NULL; cur = cur->next) count++;
    var_node** arr = malloc(count * sizeof(var_node*));
    if (!arr) return;
    int idx = 0;
    for (var_node* cur = var_list; cur != NULL; cur = cur->next) arr[idx++] = cur;
    for (int i = count - 1; i >= 0; i--) {
        printf("%s=%s\n", arr[i]->name, arr[i]->value);
    }
    free(arr);
}

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
    "set",
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
        printf("  set                 Display shell variables\n");
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

    if (strcmp(arglist[0], "set") == 0) {
        print_variables();
        return 1;
    }

    return 0;
}