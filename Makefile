
CC?=gcc
CFLAGS?=-Wall
DESTDIR?=/
PREFIX?=usr

cmux: cmux.c
	$(CC) $(CFLAGS) cmux.c -o cmux

install: cmux
	install -m 0755 cmux $(DESTDIR)/$(PREFIX)/bin/cmux

clean:
	-@rm cmux

.PHONY: install clean
