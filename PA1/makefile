CC = gcc
CFLAGS = -Wall -Wextra -O2



shell: shell.c parser.c mpc.c parser.h mpc.h
	$(CC) $(CFLAGS) shell.c parser.c mpc.c -o shell



clean:
	rm -f shell 