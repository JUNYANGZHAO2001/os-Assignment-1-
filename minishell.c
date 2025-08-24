/*********************************************************************
   Program  : miniShell                   
 --------------------------------------------------------------------
   Modified minishell with:
   - Background processes (&)
   - Reporting job completion
   - Proper "cd" handling
   - perror after syscalls
   - exec failure handling
********************************************************************/

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#define NV 20    /* max number of command tokens */
#define NL 100   /* input buffer size */

char line[NL];   /* command input buffer */

typedef struct {
    int job_id;
    pid_t pid;
    char command[NL];
} Job;

Job jobs[100];
int job_count = 0;
int next_job_id = 1;

/*
  shell prompt
*/
void prompt(void) {
    fprintf(stdout, "\n msh> ");
    fflush(stdout);
}

/* check if background jobs finished */
void check_background_jobs() {
    int status;
    pid_t pid;
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].pid != 0) {
            pid = waitpid(jobs[i].pid, &status, WNOHANG);
            if (pid > 0) {
                printf("[%d]+ Done                 %s\n",
                       jobs[i].job_id,
                       jobs[i].command);
                fflush(stdout);
                jobs[i].pid = 0; // mark as done
            }
        }
    }
}

/* reconstruct command string from argv[] */
void build_command_string(char *dest, char *argv[]) {
    dest[0] = '\0';
    for (int i = 0; argv[i] != NULL; i++) {
        strcat(dest, argv[i]);
        if (argv[i+1] != NULL) strcat(dest, " ");
    }
}

int main(int argk, char *argv[], char *envp[]) {
    int frkRtnVal;      /* value returned by fork sys call */
    char *v[NV];        /* array of pointers to command line tokens */
    char *sep = " \t\n";/* command line token separators    */
    int i;

    while (1) { /* do Forever */
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

        /* handle "cd" */
        if (strcmp(v[0], "cd") == 0) {
            char *dir = v[1] ? v[1] : getenv("HOME");
            if (chdir(dir) == -1) {
                perror("cd");
            }
            check_background_jobs();
            continue;
        }

        /* check if last arg is & */
        int background = 0;
        if (i > 0 && v[i-1] && strcmp(v[i-1], "&") == 0) {
            background = 1;
            v[i-1] = NULL; // remove "&" from args
        }

        /* fork */
        frkRtnVal = fork();
        if (frkRtnVal == -1) {
            perror("fork");
            continue;
        }
        if (frkRtnVal == 0) { /* child */
            execvp(v[0], v);
            perror("execvp"); /* exec failed */
            exit(1);
        } else { /* parent */
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

