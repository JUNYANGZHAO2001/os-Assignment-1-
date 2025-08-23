#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

// Custom signal handler
void signal_handler(int sig) {
    if (sig == SIGHUP) {
        printf("Ouch!\n");
        fflush(stdout);
    } else if (sig == SIGINT) {
        printf("Yeah!\n");
        fflush(stdout);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <n>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int n = atoi(argv[1]);
    if (n <= 0) {
        fprintf(stderr, "Error: n must be a positive integer.\n");
        exit(EXIT_FAILURE);
    }

    // Register signal handlers
    if (signal(SIGHUP, signal_handler) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < n; i++) {
        printf("%d\n", i * 2);
        fflush(stdout);
        sleep(5); // Slow execution to allow signal testing
    }

    return 0;
}
