/*********************************************************************
   Program  : miniShell                   Version    : 1.3
 --------------------------------------------------------------------
   skeleton code for linix/unix/minix command line interpreter
 --------------------------------------------------------------------
   File			: minishell.c
   Compiler/System	: gcc/linux

********************************************************************/

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#define NV 20
#define NL 100

char line[NL];

typedef struct {
    int job_id;
    pid_t pid;
    char command[NL];
} Job;

Job jobs[100];
int job_count = 0;
int next_job_id = 1;

void prompt(void) {
    printf("\nmsh> ");
    fflush(stdout);
}

void check_background_jobs(void) {
    int status;
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].pid != 0) {
            pid_t r = waitpid(jobs[i].pid, &status, WNOHANG);
            if (r > 0) {
                printf("[%d]+ Done                 %s\n", jobs[i].job_id, jobs[i].command);
                fflush(stdout);
                jobs[i].pid = 0; // mark finished
            }
        }
    }
}

void build_command_string(char *dest, char *argv[]) {
    dest[0] = '\0';
    for (int i = 0; argv[i] != NULL; i++) {
        strcat(dest, argv[i]);
        if (argv[i+1] != NULL) strcat(dest, " ");
    }
}

int main(int argk, char *argv[], char *envp[]) {
    int frkRtnVal;
    char *v[NV];
    char *sep = " \t\n";
    int i;

    while (1) {
        prompt();

        if (fgets(line, NL, stdin) == NULL) {
            if (feof(stdin)) exit(0);
            continue;
        }

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') {
            check_background_jobs();
            continue;
        }

        v[0] = strtok(line, sep);
        for (i = 1; i < NV; i++) {
            v[i] = strtok(NULL, sep);
            if (v[i] == NULL) break;
        }

        if (v[0] == NULL) continue;

        // built-in cd
        if (strcmp(v[0], "cd") == 0) {
            char *dir = v[1] ? v[1] : getenv("HOME");
            if (chdir(dir) == -1) {
                perror("cd");
            }
            check_background_jobs();
            continue;
        }

        // check background
        int background = 0;
        if (i > 0 && v[i-1] && strcmp(v[i-1], "&") == 0) {
            background = 1;
            v[i-1] = NULL;
        }

        frkRtnVal = fork();
        if (frkRtnVal < 0) {
            perror("fork");
            continue;
        }

        if (frkRtnVal == 0) { // child
            execvp(v[0], v);
            perror("execvp");
            _exit(1);
        } else { // parent
            if (background) {
                jobs[job_count].job_id = next_job_id++;
                jobs[job_count].pid = frkRtnVal;
                build_command_string(jobs[job_count].command, v);
                printf("[%d] %d\n", jobs[job_count].job_id, frkRtnVal);
                fflush(stdout);
                job_count++;
            } else {
                if (waitpid(frkRtnVal, NULL, 0) == -1) {
                    perror("waitpid");
                }
            }
        }

        check_background_jobs();
    }
}
