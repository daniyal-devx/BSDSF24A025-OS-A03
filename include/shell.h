#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_LEN 512
#define MAXARGS 10
#define ARGLEN 30
#define PROMPT "FCIT> "
#define HISTORY_SIZE 20
#define MAX_JOBS 64

typedef struct {
    pid_t pid;
    char* cmd;
} job_t;

// Function prototypes
char* read_cmd(char* prompt, FILE* fp);
char** tokenize(char* cmdline);
int execute(char** arglist);
int handle_builtin(char** arglist);
void add_to_history(const char* cmd);
void print_history(void);
char* get_history_command(int n);
void setup_readline(void);
int process_command(char* cmdline);
int execute_command_get_status(char* cmdline);
void add_job(pid_t pid, const char* cmd);
void remove_job_by_pid(pid_t pid);
void reap_jobs(void);
void print_jobs(void);
int is_assignment(const char* arg);
int handle_assignment(const char* arg);
void expand_variables(char** arglist);
void print_variables(void);

#endif // SHELL_H
