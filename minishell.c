/*********************************************************************
   Program  : miniShell                   Version    : 2.0
 --------------------------------------------------------------------
   Minimal Unix shell with background jobs and cd built-in
 --------------------------------------------------------------------
   File      : minishell.c
   Compiler  : gcc/linux
********************************************************************/

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#define NV 20        /* max number of command tokens */
#define NL 100       /* input buffer size */

char line[NL];       /* command input buffer */

typedef struct job {
    int id;
    pid_t pid;
    char cmd[NL];
    struct job *next;
} job;

job *jobs = NULL;
int job_counter = 1;

/* Add a job to list */
void add_job(pid_t pid, char *cmd) {
    job *j = malloc(sizeof(job));
    j->id = job_counter++;
    j->pid = pid;
    strncpy(j->cmd, cmd, NL-1);
    j->cmd[NL-1] = '\0';
    j->next = jobs;
    jobs = j;
    printf("[%d] %d\n", j->id, j->pid);
    fflush(stdout);
}

/* Check for finished background jobs */
void check_jobs() {
    job **prev = &jobs;
    job *j = jobs;
    int status;
    while (j) {
        pid_t done = waitpid(j->pid, &status, WNOHANG);
        if (done > 0) {
            printf("[%d]+ Done\t\t%s\n", j->id, j->cmd);
            fflush(stdout);
            *prev = j->next;
            free(j);
            j = *prev;
        } else {
            prev = &j->next;
            j = j->next;
        }
    }
}

/* shell prompt */
void prompt(void) {
    fprintf(stdout, "\nmsh> ");
    fflush(stdout);
}

int main(int argk, char *argv[], char *envp[]) {
    int frkRtnVal;
    char *v[NV];              /* command tokens */
    char *sep = " \t\n";      /* separators */
    int i;

    while (1) {
        prompt();
        if (fgets(line, NL, stdin) == NULL) {
            exit(0);
        }

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') {
            continue;   /* skip comments/empty lines */
        }

        v[0] = strtok(line, sep);
        for (i = 1; i < NV; i++) {
            v[i] = strtok(NULL, sep);
            if (v[i] == NULL) break;
        }

        int background = 0;
        if (i > 0 && v[i-1] && strcmp(v[i-1], "&") == 0) {
            background = 1;
            v[i-1] = NULL;  /* remove '&' */
        }

        /* Built-in command: cd */
        if (strcmp(v[0], "cd") == 0) {
            if (v[1] == NULL) {
                fprintf(stderr, "cd: missing operand\n");
            } else {
                if (chdir(v[1]) != 0) {
                    perror("cd");
                }
            }
            check_jobs();
            continue;
        }

        /* Built-in command: exit */
        if (strcmp(v[0], "exit") == 0) {
            exit(0);
        }

        /* Fork a child process */
        frkRtnVal = fork();
        if (frkRtnVal < 0) {
            perror("fork");
            continue;
        }

        if (frkRtnVal == 0) {   /* child */
            execvp(v[0], v);
            perror("execvp");   /* if exec fails */
            exit(1);
        } else {                /* parent */
            if (background) {
                add_job(frkRtnVal, v[0]);
            } else {
                int status;
                if (waitpid(frkRtnVal, &status, 0) < 0) {
                    perror("waitpid");
                }
            }
        }
        check_jobs();
    }
}
