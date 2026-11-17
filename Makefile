PROG = server
SRCS = server.c
CC = cc

CFLAGS = -Wall -Wextra -Wpedantic -std=c99
DEBUGFLAGS = -O0 -g -fno-omit-frame-pointer \
	     -fsanitize=address,undefined

all: ${PROG}

${PROG}: ${SRCS}
	${CC} ${CFLAGS} -o ${.TARGET} ${.ALLSRC}

debug: ${PROG}.debug

${PROG}.debug: ${SRCS}
	${CC} ${CFLAGS} ${DEBUGFLAGS} -o ${.TARGET} ${.ALLSRC}

clean:
	rm -f ${PROG} ${PROG}.debug

.PHONY: all clean debug
