/*********************************************************************
Program : miniShell Version : 1.3+
--------------------------------------------------------------------
Simple POSIX command-line interpreter (assignment version)
--------------------------------------------------------------------
File : minishell.c
Compiler/System : gcc/linux
********************************************************************/

#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#define NV  64   /* max number of command tokens */
#define NL  256  /* input buffer size */
#define MAX_JOBS 128

static char line[NL]; /* command input buffer */

struct job {
    int   id;            /* job number (1-based) */
    pid_t pid;           /* background process id */
    int   active;        /* 1 if running */
    char  cmd[NL];       /* reconstructed command line (without &) */
};

static struct job jobs[MAX_JOBS];
static int job_count = 0;

/* set by SIGCHLD handler; reaped synchronously in main loop */
static volatile sig_atomic_t child_exited = 0;

/* prompt only when interactive */
static void prompt(void) {
    if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        fprintf(stdout, "msh> ");
        fflush(stdout);
    }
}

/* find job by pid */
static int find_job_index(pid_t pid) {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].active && jobs[i].pid == pid) return i;
    }
    return -1;
}

/* reap any finished background children and report "Done" */
static void reap_background_jobs(void) {
    int status;
    pid_t pid;

    for (;;) {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid == 0) {
            /* no more finished children */
            break;
        }
        if (pid < 0) {
            if (errno == ECHILD) break; /* nothing to reap */
            perror("waitpid");
            break;
        }
        int idx = find_job_index(pid);
        if (idx >= 0) {
            jobs[idx].active = 0;
            /* Match sample formatting: "[#]+ Done                 <cmd>" */
            /* (spaces are purely cosmetic for alignment) */
            fprintf(stdout, "[%d]+ Done                 %s\n",
                    jobs[idx].id, jobs[idx].cmd);
            fflush(stdout);
        }
    }
}

/* Set a flag (async-signal-safe) and defer I/O to main loop */
static void sigchld_handler(int sig) {
    (void)sig;
    child_exited = 1;
}

/* reconstruct command string from argv (excluding trailing &) */
static void build_cmdline(char *dst, size_t dstsz, char *const argv[]) {
    dst[0] = '\0';
    size_t used = 0;
    for (int i = 0; argv[i]; i++) {
        const char *tok = argv[i];
        if (i) {
            if (used + 1 < dstsz) { dst[used++] = ' '; dst[used] = '\0'; }
        }
        size_t len = strlen(tok);
        if (used + len < dstsz) {
            memcpy(dst + used, tok, len);
            used += len;
            dst[used] = '\0';
        } else {
            /* truncate safely */
            size_t space = dstsz > used ? dstsz - used - 1 : 0;
            if (space > 0) {
                memcpy(dst + used, tok, space);
                used += space;
                dst[used] = '\0';
            }
            break;
        }
    }
}

int main(int argk, char *argv[], char *envp[]) {
    (void)argk; (void)argv; (void)envp;

    /* install SIGCHLD handler (defer reaping/printing to main loop) */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);             /* not a system call, no perror */
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("sigaction");
        exit(1);
    }

    for (;;) {
        if (child_exited) {               /* report finished background jobs */
            child_exited = 0;
            reap_background_jobs();
        }

        prompt();

        if (fgets(line, NL, stdin) == NULL) {
            if (feof(stdin)) exit(0);
            perror("fgets");
            clearerr(stdin);
            continue;
        }

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') {
            continue; /* ignore comments/blank lines */
        }

        /* tokenize */
        char *v[NV];
        int i = 0;
        char *sep = " \t\n";
        v[i] = strtok(line, sep);
        if (!v[i]) continue;
        for (i = 1; i < NV; i++) {
            v[i] = strtok(NULL, sep);
            if (v[i] == NULL) break;
        }
        /* guarantee NULL-terminated argv for execvp */
        if (i == NV) v[NV-1] = NULL;
        else v[i] = NULL;

        /* built-in: exit */
        if (strcmp(v[0], "exit") == 0) {
            exit(0);
        }

        /* built-in: cd */
        if (strcmp(v[0], "cd") == 0) {
            const char *target = NULL;
            if (v[1] == NULL) {
                target = getenv("HOME"); /* cd with no args -> $HOME */
                if (!target) target = "/"; /* fallback */
            } else {
                target = v[1];
            }
            if (chdir(target) < 0) {
                perror("chdir");
            }
            continue;
        }

        /* background? (last token is "&") */
        int background = 0;
        int argc = 0;
        while (v[argc]) argc++;
        if (argc > 0 && strcmp(v[argc-1], "&") == 0) {
            background = 1;
            v[argc-1] = NULL; /* remove '&' from argv */
            argc--;
        }

        /* fork & exec */
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            continue;
        }

        if (pid == 0) {
            /* child */
            execvp(v[0], v);
            /* only reached if exec fails */
            perror("execvp");
            _exit(127); /* ensure child terminates */
        }

        /* parent */
        if (background) {
            if (job_count < MAX_JOBS) {
                jobs[job_count].id     = job_count + 1;
                jobs[job_count].pid    = pid;
                jobs[job_count].active = 1;
                build_cmdline(jobs[job_count].cmd, sizeof(jobs[job_count].cmd), v);
                job_count++;

                /* immediate job start notice: "[id] pid" */
                fprintf(stdout, "[%d] %d\n", job_count, pid);
                fflush(stdout);
            } else {
                fprintf(stderr, "Too many background jobs\n");
            }
            /* don't wait here */
        } else {
            /* foreground: wait for this specific child */
            if (waitpid(pid, NULL, 0) < 0) {
                perror("waitpid");
            }
        }
    }
}
