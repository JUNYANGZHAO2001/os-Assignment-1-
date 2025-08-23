CC=gcc
CFLAGS=-Wall -Wextra -g

all: even minishell

even: even.c
	$(CC) $(CFLAGS) -o even even.c

minishell: minishell.c
	$(CC) $(CFLAGS) -o minishell minishell.c

clean:
	rm -f even minishell
