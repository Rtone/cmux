DESTDIR=/
prefix=usr

ifeq ($(DEB_BUILD_GNU_TYPE),$(DEB_HOST_GNU_TYPE))
       CC=gcc
else
       CC=$(DEB_HOST_GNU_TYPE)-gcc
endif


cmux:
	$(CC) cmux.c -o cmux

install: cmux
	install -m 0755 cmux $(DESTDIR)/$(prefix)/bin/cmux

clean:
	-@rm cmux
.PHONY: install clean



