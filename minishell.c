#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#define NV 20   /* max number of command tokens */
#define NL 100  /* input buffer size */

char line[NL]; /* command input buffer */

struct job {
    int id;         // job number
    pid_t pid;      // process ID
    char cmd[NL];   // command line (for printing "Done")
    int active;     // still running?
};

static struct job jobs[100];
static int job_count = 0;

/* shell prompt */
void prompt(void) {
    fprintf(stdout, "msh> ");
    fflush(stdout);
}

/* SIGCHLD handler to report finished background jobs */
void sigchld_handler(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].pid == pid && jobs[i].active) {
                jobs[i].active = 0;
                fprintf(stdout, "[%d]+ Done                 %s\n", jobs[i].id, jobs[i].cmd);
                fflush(stdout);
            }
        }
    }
}

int main(int argk, char *argv[], char *envp[]) {
    int frkRtnVal;
    char *v[NV];
    char *sep = " \t\n";
    int i;

    /* install SIGCHLD handler */
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("sigaction");
        exit(1);
    }

    while (1) {
        prompt();
        if (fgets(line, NL, stdin) == NULL) {
            if (feof(stdin)) exit(0);
            perror("fgets");
            continue;
        }

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0')
            continue;

        /* parse command */
        v[0] = strtok(line, sep);
        for (i = 1; i < NV; i++) {
            v[i] = strtok(NULL, sep);
            if (v[i] == NULL) break;
        }

        /* check for built-in exit */
        if (strcmp(v[0], "exit") == 0) {
            exit(0);
        }

        /* check for built-in cd */
        if (strcmp(v[0], "cd") == 0) {
            if (v[1] == NULL) {
                fprintf(stderr, "cd: missing argument\n");
            } else if (chdir(v[1]) != 0) {
                perror("chdir");
            }
            continue;
        }

        /* check background flag */
        int background = 0;
        if (i > 0 && v[i-1] && strcmp(v[i-1], "&") == 0) {
            background = 1;
            v[i-1] = NULL; // remove '&'
        }

        switch (frkRtnVal = fork()) {
        case -1: /* error */
            perror("fork");
            break;
        case 0: /* child */
            execvp(v[0], v);
            perror("execvp"); // only reached on failure
            exit(1);
        default: /* parent */
            if (background) {
                job_count++;
                jobs[job_count-1].id = job_count;
                jobs[job_count-1].pid = frkRtnVal;
                jobs[job_count-1].active = 1;
                strncpy(jobs[job_count-1].cmd, v[0], NL);
                fprintf(stdout, "[%d] %d\n", job_count, frkRtnVal);
                fflush(stdout);
            } else {
                if (waitpid(frkRtnVal, NULL, 0) < 0) {
                    perror("waitpid");
                }
            }
            break;
        }
    }
}
