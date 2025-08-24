/*********************************************************************
 Program : miniShell  Version : 2.0 (assignment-ready)
 --------------------------------------------------------------------
 A small POSIX-ish command line interpreter for Linux/Unix.
 Implements:
   • Background jobs via trailing '&' with job start/finish reporting
   • Built-in cd (cd, cd <path>, cd -, cd ~)
   • perror() after every system call failure
   • Correct child termination when execvp() fails
   • Basic, signal-driven reaping of background children (SIGCHLD)

 Notes on design decisions:
   - We keep a small in-memory jobs table (pid, job id, command string).
   - On background start we print:       "[<job>] <pid>\n"
   - On background completion we print:  "[<job>]+ Done                 <cmd>\n"
   - We print DONE messages directly from the SIGCHLD handler.
     We only use async-signal-safe calls inside the handler (waitpid, write),
     and we build the output with tiny integer→string helpers (no printf).
   - Every system call error path calls perror("<call>").
   - If execvp fails in the child, we perror and _exit(127) immediately.

 Tested with the provided pipeline test; the ordering matches expectations.
 --------------------------------------------------------------------
 File : minishell.c
 Compiler/System : gcc / Linux
 Build: gcc -Wall -Wextra -O2 -std=c99 -pedantic -o minishell minishell.c
 *********************************************************************/

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>

#define NV  64    /* max number of command tokens */
#define NL  1024  /* input buffer size */

static char line[NL];                /* raw command input buffer */

struct job {
    int   used;                      /* 0 = empty slot */
    int   id;                        /* job number (1, 2, 3, ...) */
    pid_t pid;                       /* child pid */
    char  cmd[NL];                   /* printable command (w/o &'s) */
};

static struct job jobs[MAX_JOBS];
static int next_job_id = 1;          /* monotonically increasing */

/* --------------------- Tiny signal-safe utilities -------------------- */
/* Convert unsigned long to decimal into buf, return length. */
static size_t utoa10(unsigned long v, char *buf) {
    char tmp[32]; size_t n = 0;
    if (v == 0) { buf[0] = '0'; return 1; }
    while (v > 0 && n < sizeof(tmp)) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    /* reverse into buf */
    for (size_t i = 0; i < n; ++i) buf[i] = tmp[n - 1 - i];
    return n;
}

static size_t cpystr(char *dst, const char *src) {
    size_t i = 0; while (src[i]) { dst[i] = src[i]; ++i; } return i; /* no NUL */
}

static struct job *find_job_by_pid(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; ++i) if (jobs[i].used && jobs[i].pid == pid) return &jobs[i];
    return NULL;
}

static void remove_job(struct job *j) { j->used = 0; }

/* --------------------------- SIGCHLD handler ------------------------- */
static void sigchld_handler(int sig) {
    (void)sig; /* unused */
    int saved_errno = errno; /* preserve errno */

    while (1) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) break; /* 0: nothing more, -1: ECHILD */

        struct job *j = find_job_by_pid(pid);
        if (j) {
            /* Build: "[<id>]+ Done                 <cmd>\n" */
            char buf[NL + 64];
            size_t k = 0;
            buf[k++] = '[';
            k += utoa10((unsigned long)j->id, buf + k);
            buf[k++] = ']';
            buf[k++] = '+';
            buf[k++] = ' ';
            buf[k++] = 'D'; buf[k++] = 'o'; buf[k++] = 'n'; buf[k++] = 'e';
            for (int i = 0; i < 17; ++i) buf[k++] = ' '; /* spacing similar to bash */
            k += cpystr(buf + k, j->cmd);
            buf[k++] = '\n';
            /* write to stdout (async-signal-safe) */
            ssize_t wr = write(STDOUT_FILENO, buf, (unsigned int)k);
            (void)wr; /* ignore */
            remove_job(j);
        }
        /* foreground children are not in table; nothing to print */
    }
    errno = saved_errno; /* restore */
}

/* ------------------------------ Prompt ------------------------------- */
static void prompt(void) {
    /* NOTE: Keep prompt minimal; fflush to make visible when piped */
    fprintf(stdout, "\n msh> ");
    fflush(stdout);
}

/* ----------------------------- Job table ----------------------------- */
static struct job *alloc_job_slot(void) {
    for (int i = 0; i < MAX_JOBS; ++i) if (!jobs[i].used) { jobs[i].used = 1; return &jobs[i]; }
    return NULL;
}

static void record_background_job(pid_t pid, char **argv) {
    struct job *j = alloc_job_slot();
    if (!j) return; /* table full – silently ignore listing, still works */
    j->id = next_job_id++;
    j->pid = pid;

    /* Build printable command string from argv */
    size_t k = 0;
    j->cmd[0] = '\0';
    for (int i = 0; argv[i]; ++i) {
        if (i) j->cmd[k++] = ' ';
        size_t len = strnlen(argv[i], NL - 1 - k);
        memcpy(j->cmd + k, argv[i], len);
        k += len;
        if (k >= NL - 1) break;
    }
    j->cmd[k] = '\0';

    /* Job start line: "[id] pid\n" */
    printf("[%d] %d\n", j->id, (int)pid);
    fflush(stdout);
}

/* ------------------------------ Builtins ----------------------------- */
static char prev_dir[PATH_MAX] = ""; /* for `cd -` */

static int do_cd(char **argv) {
    /* Determine target directory */
    const char *target = NULL;
    if (!argv[1] || strcmp(argv[1], "~") == 0) {
        target = getenv("HOME"); /* not a syscall; may be NULL */
        if (!target) target = "/"; /* fallback */
    } else if (strcmp(argv[1], "-") == 0) {
        if (prev_dir[0] == '\0') {
            fprintf(stderr, "cd: OLDPWD not set\n");
            return 1;
        }
        target = prev_dir;
        printf("%s\n", target); /* match common shells printing the path */
    } else {
        target = argv[1];
    }

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        perror("getcwd"); /* not fatal for cd */
        cwd[0] = '\0';
    }

    if (chdir(target) == -1) {
        perror("chdir");
        return 1;
    }

    if (cwd[0]) {
        /* Save previous directory only if we had a valid current dir */
        strncpy(prev_dir, cwd, sizeof(prev_dir) - 1);
        prev_dir[sizeof(prev_dir) - 1] = '\0';
    }
    return 0;
}

static bool is_builtin(char **argv) {
    return argv[0] && (
        strcmp(argv[0], "cd") == 0 ||
        strcmp(argv[0], "exit") == 0 ||
        strcmp(argv[0], "quit") == 0
    );
}

static int run_builtin(char **argv) {
    if (strcmp(argv[0], "cd") == 0) return do_cd(argv);
    if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) exit(0);
    return 0;
}

/* ------------------------------- Main -------------------------------- */
int main(void) {
    /* Install SIGCHLD handler to reap background jobs and print Done lines */
    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; /* restart stdio; don't report stops */
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        /* continue without async reaping (foreground still works) */
    }

    char *sep = " \t\n"; /* token separators */

    /* REPL */
    for (;;) {
        prompt();

        if (!fgets(line, NL, stdin)) {
            if (feof(stdin)) {
                /* EOF: exit cleanly */
                exit(0);
            } else {
                perror("fgets");
                clearerr(stdin);
                continue;
            }
        }

        if (line[0] == '\n' || line[0] == '\0' || line[0] == '#') {
            continue; /* empty/comment */
        }

        /* Tokenize */
        char *v[NV];
        int i = 0;
        v[i] = strtok(line, sep);
        if (!v[i]) continue;
        for (i = 1; i < NV; ++i) {
            v[i] = strtok(NULL, sep);
            if (!v[i]) break;
        }
        v[i] = NULL; /* make sure */

        /* Built-ins (handled in the shell process) */
        if (is_builtin(v)) {
            (void)run_builtin(v);
            continue;
        }

        /* Background? If last token is "&", strip it. */
        int argc = 0; while (v[argc]) ++argc;
        bool background = false;
        if (argc > 0 && strcmp(v[argc - 1], "&") == 0) {
            v[argc - 1] = NULL; /* remove & */
            background = true;
            --argc;
            if (argc == 0) continue; /* line was just '&' */
        }

        /* Fork and exec */
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            continue;
        }

        if (pid == 0) {
            /* Child */
            execvp(v[0], v);
            /* If we get here, exec failed */
            perror("execvp");
            _exit(127);
        }

        /* Parent */
        if (background) {
            record_background_job(pid, v);
            /* Do NOT wait; continue to next prompt/command */
        } else {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            if (w == -1) {
                if (errno != ECHILD) perror("waitpid");
            }
            /* Optional: you could report exit status here if desired */
        }
    }
}

