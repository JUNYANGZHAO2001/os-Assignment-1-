/*********************************************************************
   Program  : miniShell                   Version    : 2.1
 --------------------------------------------------------------------
   Requirements implemented:
   - Background jobs (&) with immediate start message: "[#] ####"
   - Report completion: "[#]+ Done                 <command and args>"
   - Built-in cd (parent changes directory; "cd" -> $HOME)
   - perror() after each system call
   - Child exits cleanly if execvp() fails
   - POSIX API only
 --------------------------------------------------------------------
   File     : minishell.c
   Build    : gcc -Wall -Wextra -g -o minishell minishell.c
********************************************************************/

#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define NV  64     /* max number of tokens (argv size) */
#define NL  256    /* input buffer size (one command per line) */

static char line[NL];  /* command input buffer */

/* ---------------- Background Job List ---------------- */

typedef struct Job {
    int   id;
    pid_t pid;
    char  cmd[NL];     /* command (without the trailing &) */
    struct Job *next;
} Job;

static Job *job_list = NULL;
static int   job_counter = 1;

/* Build a printable command string from argv (null-terminated). */
static void build_cmd_string(char *dst, size_t dstsz, char *const argv[]) {
    dst[0] = '\0';
    size_t used = 0;
    for (int i = 0; argv[i] != NULL; i++) {
        const char *word = argv[i];
        size_t wlen = strlen(word);
        if (used + (i ? 1 : 0) + wlen + 1 >= dstsz) break; /* leave space for NUL */
        if (i) { dst[used++] = ' '; }
        memcpy(dst + used, word, wlen);
        used += wlen;
        dst[used] = '\0';
    }
}

/* Add a background job; print "[#] ####" right away */
static void add_job(pid_t pid, char *const argv[]) {
    Job *j = (Job *)malloc(sizeof(Job));
    if (!j) {
        perror("malloc");
        return;
    }
    j->id  = job_counter++;
    j->pid = pid;
    build_cmd_string(j->cmd, sizeof(j->cmd), argv);
    j->next = job_list;
    job_list = j;

    /* Match expected start message format exactly: "[#] ####" */
    printf("[%d] %d\n", j->id, (int)j->pid);
    fflush(stdout);
}

/* Reap finished background jobs and print completion lines */
static void check_jobs(void) {
    Job **pp = &job_list;
    while (*pp) {
        int status = 0;
        pid_t r = waitpid((*pp)->pid, &status, WNOHANG);
        if (r < 0) {
            perror("waitpid");
            /* On error, drop the job to avoid leaking */
            Job *dead = *pp;
            *pp = dead->next;
            free(dead);
            continue;
        }
        if (r > 0) {
            /* Job finished */
            printf("[%d]+ Done                 %s\n", (*pp)->id, (*pp)->cmd);
            fflush(stdout);
            Job *done = *pp;
            *pp = done->next;
            free(done);
        } else {
            pp = &((*pp)->next);
        }
    }
}

/* ---------------- Prompt ---------------- */

static void prompt(void) {
    /* Keep it minimal and consistent with the starter */
    fprintf(stdout, "\n msh> ");
    fflush(stdout);
}

/* ---------------- Parser ---------------- */

static int parse_line(char *buf, char *argv[], int *is_bg) {
    /* Tokenize by whitespace; simple, POSIX-y behavior */
    const char *sep = " \t\r\n";
    int argc = 0;
    char *tok = strtok(buf, sep);
    while (tok && argc < NV - 1) {
        argv[argc++] = tok;
        tok = strtok(NULL, sep);
    }
    argv[argc] = NULL;

    *is_bg = 0;
    if (argc > 0 && strcmp(argv[argc - 1], "&") == 0) {
        *is_bg = 1;
        argv[argc - 1] = NULL;   /* remove '&' from argv */
        argc--;
    }
    return argc;
}

/* ---------------- Built-ins ---------------- */

static int builtin_cd(char *argv[]) {
    /* cd [dir]; if no dir, go to $HOME; print errors with perror */
    const char *target = argv[1];
    if (!target) {
        target = getenv("HOME");
        if (!target) target = "/";
    }
    if (chdir(target) < 0) {
        perror("chdir");
        return -1;
    }
    return 0;
}

/* ---------------- Main ---------------- */

int main(int argk, char *argv[], char *envp[]) {
    (void)argk; (void)argv; (void)envp;

    for (;;) {
        check_jobs();      /* report finished background work */
        prompt();

        if (fgets(line, sizeof(line), stdin) == NULL) {
            if (feof(stdin)) {
                /* EOF from stdin (e.g., piped script ends): exit cleanly */
                return 0;
            }
            /* fgets error */
            perror("fgets");
            clearerr(stdin);
            continue;
        }

        /* Skip comments and blank lines */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0')
            continue;

        char *argvv[NV];
        int is_bg = 0;
        int argc = parse_line(line, argvv, &is_bg);
        if (argc == 0) continue;

        /* Built-in: cd */
        if (strcmp(argvv[0], "cd") == 0) {
            (void)builtin_cd(argvv);
            /* After any command, also check background completions */
            check_jobs();
            continue;
        }

        /* Built-in: exit (nice to have) */
        if (strcmp(argvv[0], "exit") == 0) {
            return 0;
        }

        /* Fork to run external command */
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            /* keep shell running */
            continue;
        }

        if (pid == 0) {
            /* Child: exec the program. On failure, perror then exit. */
            execvp(argvv[0], argvv);
            perror("execvp");
            _exit(127); /* 127 is common for command-not-found/exec fail */
        }

        /* Parent */
        if (is_bg) {
            /* Background: record job and DO NOT wait now */
            add_job(pid, argvv);
        } else {
            /* Foreground: wait for this specific child */
            int status = 0;
            if (waitpid(pid, &status, 0) < 0) {
                perror("waitpid");
            }
            /* After foreground finishes, also report any background completions */
            check_jobs();
        }
    }
}

