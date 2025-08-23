#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#define MAXLINE 1024
#define MAXARGS 128

typedef struct Job {
    int id;
    pid_t pid;
    char command[MAXLINE];
    struct Job *next;
} Job;

Job *job_list = NULL;
int job_counter = 1;

// Add a background job
void add_job(pid_t pid, const char *cmd) {
    Job *new_job = malloc(sizeof(Job));
    if (!new_job) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    new_job->id = job_counter++;
    new_job->pid = pid;
    strncpy(new_job->command, cmd, MAXLINE);
    new_job->command[MAXLINE - 1] = '\0';
    new_job->next = job_list;
    job_list = new_job;

    printf("[%d] %d\n", new_job->id, new_job->pid);
    fflush(stdout);
}

// Check for finished background jobs
void check_jobs() {
    Job **current = &job_list;
    while (*current) {
        int status;
        pid_t result = waitpid((*current)->pid, &status, WNOHANG);
        if (result > 0) {
            printf("[%d]+ Done                 %s\n", (*current)->id, (*current)->command);
            fflush(stdout);

            Job *finished = *current;
            *current = finished->next;
            free(finished);
        } else {
            current = &((*current)->next);
        }
    }
}

// Parse command line into argv
int parse_line(char *line, char **argv, int *background) {
    char *token;
    int argc = 0;
    *background = 0;

    token = strtok(line, " \t\n");
    while (token != NULL && argc < MAXARGS - 1) {
        if (strcmp(token, "&") == 0) {
            *background = 1;
        } else {
            argv[argc++] = token;
        }
        token = strtok(NULL, " \t\n");
    }
    argv[argc] = NULL;

    return argc;
}

int main() {
    char line[MAXLINE];
    char *argv[MAXARGS];

    while (1) {
        check_jobs(); // reap finished jobs
        printf("minishell> ");
        fflush(stdout);

        if (fgets(line, MAXLINE, stdin) == NULL) {
            if (feof(stdin)) break; // Ctrl+D exits
            perror("fgets");
            continue;
        }

        int background;
        int argc = parse_line(line, argv, &background);
        if (argc == 0) continue; // skip empty lines

        // Handle built-in cd
        if (strcmp(argv[0], "cd") == 0) {
            if (argc < 2) {
                char *home = getenv("HOME");
                if (home == NULL) home = "/";
                if (chdir(home) < 0) perror("chdir");
            } else {
                if (chdir(argv[1]) < 0) perror("chdir");
            }
            continue;
        }

        // Fork a child process
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            continue;
        }

        if (pid == 0) { // child
            execvp(argv[0], argv);
            perror("execvp"); // only runs if exec fails
            exit(EXIT_FAILURE);
        } else { // parent
            if (background) {
                add_job(pid, argv[0]);
            } else {
                if (waitpid(pid, NULL, 0) < 0) perror("waitpid");
            }
        }
    }

    return 0;
}
