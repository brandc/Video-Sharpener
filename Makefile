PROGRAM_NAME  ="sharpener"
CC           ?= gcc
CFLAGS       ?=-lm -lpthread

all:
	${CC} sharpener.c -o ${PROGRAM_NAME} ${CFLAGS}

debug:
	${CC} sharpener.c -Wall -ggdb -pedantic -o ${PROGRAM_NAME} ${CFLAGS}

clean :
	rm -f ${PROGRAM_NAME}



