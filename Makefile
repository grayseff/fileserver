CC = clang
CFLAGS = -std=c99 -Wall -Wpedantic -Wextra -Werror -O2

fileserver: server.c
	$(CC) $(CFLAGS) -o fileserver server.c

clean:
	rm -f fileserver

.PHONY: clean
