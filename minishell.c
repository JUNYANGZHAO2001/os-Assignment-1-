/*********************************************************************
   Program  : miniShell                   Version    : 2.0
 --------------------------------------------------------------------
   Features:
   - Run commands in foreground and background (&)
   - Report completion of background jobs
   - Built-in "cd" command
   - perror() after each system call
   - Proper child termination if execvp fails
 --------------------------------------------------------------------
   File   : minishell.c
   Compiler/System : gcc / Linux
********************************************************************/

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#define NV 20     /* max number of command tokens */
#define NL 100    /* input buffer size */
char line[NL];   /* command input buffer */

/* Background job struct */
typedef struct Job {
    int id;
    pid_t pid;
    char command[NL];
    struct Job *next;
} Job;

Job *job_list = NULL;
int job_counter = 1;

/* shell prompt */
void prompt(void) {
    printf("\nmsh> ");
    fflush(stdout);
}

/* add background job */
void add_job(pid_t pid, const char *cmd) {
    Job *new_job = malloc(sizeof(Job));
    if (!new_job) {
        perror("malloc");
        return;
    }
    new_job->id = job_counter++;
    new_job->pid = pid;
    strncpy(new_job->command, cmd, NL);
    new_job->command[NL-1] = '\0';
    new_job->next = job_list;
    job_list = new_job;

    printf("[%d] %d\n", new_job->id, new_job->pid);
    fflush(stdout);
}

/* check for finished background jobs */
void check_jobs() {
    Job **curr = &job_list;
    while (*curr) {
        int status;
        pid_t result = waitpid((*curr)->pid, &status, WNOHANG);
        if (result > 0) {
            printf("[%d]+ Done                 %s\n", (*curr)->id, (*curr)->command);
            fflush(stdout);
            Job *finished = *curr;
            *curr = finished->next;
            free(finished);
        } else {
            curr = &((*curr)->next);
        }
    }
}

int main(int argk, char *argv[], char *envp[]) {
    int frkRtnVal;              /* value returned by fork sys call */
    char *v[NV];                /* array of pointers to command line tokens */
    char *sep = " \t\n";        /* command line token separators */
    int i;

    while (1) {
        check_jobs();   /* check background jobs first */
        prompt();

        if (fgets(line, NL, stdin) == NULL) {
            if (feof(stdin)) {
                exit(0);
            }
            perror("fgets");
            continue;
        }

        /* skip empty or comment lines */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') {
            continue;
        }

        v[0] = strtok(line, sep);
        for (i = 1; i < NV; i++) {
            v[i] = strtok(NULL, sep);
            if (v[i] == NULL) break;
        }

        if (v[0] == NULL) continue;  // no command

        /* built-in "cd" */
        if (strcmp(v[0], "cd") == 0) {
            if (v[1] == NULL) {
                char *home = getenv("HOME");
                if (!home) home = "/";
                if (chdir(home) < 0) perror("chdir");
            } else {
                if (chdir(v[1]) < 0) perror("chdir");
            }
            continue;
        }

        /* check for background job (&) */
        int background = 0;
        if (v[i-1] && strcmp(v[i-1], "&") == 0) {
            background = 1;
            v[i-1] = NULL;  /* remove '&' */
        }

        /* fork a child process */
        frkRtnVal = fork();
        if (frkRtnVal < 0) {
            perror("fork");
            continue;
        }

        if (frkRtnVal == 0) {  /* child */
            execvp(v[0], v);
            perror("execvp");   /* execvp only returns on error */
            exit(EXIT_FAILURE);
        } else {                /* parent */
            if (background) {
                add_job(frkRtnVal, v[0]);
            } else {
                if (waitpid(frkRtnVal, NULL, 0) < 0) perror("waitpid");
            }
        }
    }
}
