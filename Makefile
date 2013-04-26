CC=gcc
CFLAGS=-Wall

all: cmux

program: cmux.c

clean:
	rm -f cmux

run: cmux
	./cmux