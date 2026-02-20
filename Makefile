PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
ETCDIR ?= $(PREFIX)/etc
MANDIR ?= $(PREFIX)/man
CC ?= cc
CFLAGS ?= -O2 -pipe
CFLAGS += -Wall -Wextra -pedantic -std=c11
LDFLAGS += -lX11

PROG = fluxsnap
SRCS = src/fluxsnap.c

all: $(PROG)

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

install: $(PROG)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)
	install -d $(DESTDIR)$(ETCDIR)
	install -m 0644 fluxsnap.conf $(DESTDIR)$(ETCDIR)/fluxsnap.conf.sample
	install -d $(DESTDIR)$(MANDIR)/man1
	install -m 0644 man/fluxsnap.1 $(DESTDIR)$(MANDIR)/man1/fluxsnap.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(PROG)
	rm -f $(DESTDIR)$(ETCDIR)/fluxsnap.conf.sample
	rm -f $(DESTDIR)$(MANDIR)/man1/fluxsnap.1

clean:
	rm -f $(PROG)

.PHONY: all install uninstall clean
